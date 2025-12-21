#include <cstdio>
#include <signal.h>
#include <windows.h>
#include <getopt.h>

#include "resource.h"

#ifdef USE_SDL
#define SDL_MAIN_HANDLED
extern "C" {
#include <SDL2/SDL.h>
}
#else
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "uuid.lib")

#include <samplerate.h>

#include <atomic>

#endif

#define WM_USER_TRAYICON		(WM_USER + 1)
#define WM_USER_UPDATETRAYICON	(WM_USER + 2)
#define ID_TRAY_EXIT		1001
#define ID_TRAY_MIDIPANIC	1002
#define ID_TRAY_ABOUT		1003
#define ID_TRAY_RESTART		1004

#define INTERNAL_SR 50000

#include "console.h"
#include "player.h"
#include <thread>

#define VERSION "0.6.0"

static HINSTANCE g_hInst = nullptr;
static HICON g_hIcon = nullptr;
static HICON g_hIconSleep = nullptr;
static HWND g_hWnd = nullptr;

UINT g_WM_TASKBARCREATED = UINT_MAX;

static bool g_running = true;
static bool g_paused = false;
static bool g_looping = true;
static bool g_sleeping = false;
static bool g_restart = false;

static OPLPlayer *g_player = nullptr;

static int g_srconvtype = SRC_SINC_FASTEST;
static int g_wavOutputMarginMillisecond = 1000;
static bool g_wavOutputMarginAuto = true;

#ifdef USE_SDL
static void mainLoopSDL(OPLPlayer* player, int bufferSize, bool interactive);
#else
static void mainLoopWASAPI(OPLPlayer* player, int bufferSize, bool interactive, bool traymode);
#endif
static void mainLoopWAV(OPLPlayer* player, const char* path, bool interactive);

// ----------------------------------------------------------------------------
void usage()
{
	fprintf(stderr, 
	"usage: ymfmidiwin [options] song_path [patch_path]\n"
	"\n"
	"supported song formats:  HMI, HMP, MID, MUS, RMI, XMI\n"
	"supported patch formats: AD, OPL, OP2, TMB, WOPL\n"
	"\n"
	"supported options:\n"
	"  -h / --help             show this information and exit\n"
	"  -q / --quiet            quiet (run non-interactively)\n"
	"  -1 / --play-once        play only once and then exit\n"
	"  -s / --song <num>       select an individual song, if multiple in file\n"
	"                            (default 1)\n"
	"  -o / --out <path>       output to WAV file (implies -q and -1)\n"
	"\n"
	"  -c / --chip <num>       set type of chip (1 = OPL, 2 = OPL2, 3 = OPL3; default 3)\n"
	"  -n / --num <num>        set number of chips (default 1)\n"
	"  -m / --mono             ignore MIDI panning information (OPL3 only)\n"
	"  -b / --buf <num>        set buffer size (default 4096)\n"
	"  -g / --gain <num>       set gain amount (default 1.0)\n"
	"  -r / --rate <num>       set sample rate (default 44100)\n"
	"  -f / --filter <num>     set highpass cutoff in Hz (default 5.0)\n"
	"\n"
	"  -t / --tray             resides in the task tray\n"
	"  -p / --ptime            time to enter sleep mode (msec; default 15000)\n"
	"\n"
	"  --resampler <nearest|linear|sinc_fast|sinc_medium|sinc_best>\n"
	"                          resampler type (default sinc_fast)\n"
	"  --tail-time <num>       extra tail time to append to the WAV output\n"
	"                          (msec; default auto)\n"
	"\n"
	);

	exit(1);
}

static const option options[] = 
{
	{"help",      0, nullptr, 'h'},
	{"quiet",     0, nullptr, 'q'},
	{"play-once", 0, nullptr, '1'},
	{"song",      1, nullptr, 's'},
	{"chip",      1, nullptr, 'c'},
	{"num",       1, nullptr, 'n'},
	{"mono",      0, nullptr, 'm'},
	{"buf",       1, nullptr, 'b'},
	{"gain",      1, nullptr, 'g'},
	{"rate",      1, nullptr, 'r'},
	{"filter",    1, nullptr, 'f'},
	{"tray",      0, nullptr, 't'},
	{"ptime",     0, nullptr, 'p'},
	{"resampler", 1, nullptr,  0 },
	{"tail-time", 1, nullptr,  0 },
	{0}
};

// ----------------------------------------------------------------------------
void quit(int)
{
	g_running = false;
#ifdef USE_SDL
	SDL_PauseAudio(1);
#endif
}

// ----------------------------------------------------------------------------
const char* shortPath(const char* path)
{
	const char* p;
	if ((p = strrchr(path, '\\'))
	    || (p = strrchr(path, '/')))
		return p + 1;
	
	return path;
}

std::string GetExeDirectory()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);

	std::string fullPath(path);

	size_t pos = fullPath.find_last_of("\\/");
	return fullPath.substr(0, pos);
}

BOOL WINAPI ConsoleHandler(DWORD ctrlType)
{
	switch (ctrlType)
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		g_running = false;
		return TRUE;   // 処理したことを通知
	default:
		return FALSE;
	}
}

int g_lastIconDPI = 96;
HICON g_oldhIcon = nullptr;
HICON g_oldhIconSleep = nullptr;
void ReleaseOldTrayIcon(void)
{
	// 古いアイコン削除　トレイアイコン差し替え前に削除してはいけない
	if (g_oldhIcon) {
		DestroyIcon(g_oldhIcon);
		g_oldhIcon = nullptr;
	}
	if (g_hIconSleep) {
		DestroyIcon(g_hIconSleep);
		g_hIconSleep = nullptr;
	}
}
void CreateTrayIcon(HWND hwnd)
{
	ReleaseOldTrayIcon();

	UINT dpi = GetDpiForWindow(hwnd);
	int iconSizeX = GetSystemMetrics(SM_CXSMICON) * dpi / 96;
	int iconSizeY = GetSystemMetrics(SM_CYSMICON) * dpi / 96;
	if (g_lastIconDPI != dpi) {
		// 再作成
		g_oldhIcon = g_hIcon;
		g_oldhIconSleep = g_hIconSleep;
		g_hIcon = nullptr;
		g_hIconSleep = nullptr;
	}
	if (!g_hIcon) {
		g_hIcon = (HICON)LoadImage(
			g_hInst,
			MAKEINTRESOURCE(IDI_APPICON),
			IMAGE_ICON,
			iconSizeX,
			iconSizeY,
			LR_DEFAULTCOLOR);
	}
	if (!g_hIconSleep) {
		g_hIconSleep = (HICON)LoadImage(
			g_hInst,
			MAKEINTRESOURCE(IDI_SLEEPICON),
			IMAGE_ICON,
			iconSizeX,
			iconSizeY,
			LR_DEFAULTCOLOR);
	}
	g_lastIconDPI = dpi;
}

bool RegisterTrayIcon(HWND hwnd)
{
	CreateTrayIcon(hwnd);

	NOTIFYICONDATA nid{};
	nid.cbSize = sizeof(nid);
	nid.hWnd = hwnd;
	nid.uID = 1;
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	nid.uCallbackMessage = WM_USER_TRAYICON;
	nid.hIcon = g_hIcon;

	lstrcpy(nid.szTip, TEXT("ymfmidi for Windows"));

	BOOL r = Shell_NotifyIcon(NIM_ADD, &nid);
	return !!r;
}

void UpdateTrayIcon(HWND hwnd)
{
	CreateTrayIcon(hwnd);

	NOTIFYICONDATA nid{};
	nid.cbSize = sizeof(nid);
	nid.hWnd = hwnd;
	nid.uID = 1;
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	nid.uCallbackMessage = WM_USER_TRAYICON;

	nid.hIcon = g_sleeping ? g_hIconSleep : g_hIcon;

	wcscpy_s(nid.szTip, g_sleeping ? TEXT("ymfmidi for Windows (sleep)") : TEXT("ymfmidi for Windows"));

	Shell_NotifyIcon(NIM_MODIFY, &nid);
}

int RestartApplication()
{
	// 実行ファイルパス取得
	WCHAR szFilePath[MAX_PATH] = { 0 };
	if (GetModuleFileNameW(NULL, szFilePath, MAX_PATH) == 0)
		return -1;

	// コマンドライン引数取得（exe名除く）
	// GetCommandLine()はexe名込みなので解析が必要
	LPWSTR cmdLine = GetCommandLineW();

	// コマンドラインからexe名を除去（argv[0]相当）
	int argc;
	LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
	if (!argv)
		return -1;

	// argv[0]は実行ファイルパスなので、それを除いたコマンドラインを作成
	// 例: argv[1] argv[2] ... を連結
	WCHAR szArgs[1024] = { 0 };
	for (int i = 1; i < argc; i++)
	{
		wcscat_s(szArgs, 1024, L"\"");
		wcscat_s(szArgs, 1024, argv[i]);
		wcscat_s(szArgs, 1024, L"\" ");
	}
	LocalFree(argv);

	// CreateProcessの引数用にフルコマンドラインを作成（実行ファイルパス + 引数）
	WCHAR szCmdLine[2048] = { 0 };
	swprintf_s(szCmdLine, 2048, L"\"%s\" %s", szFilePath, szArgs);

	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = {};

	BOOL ret = CreateProcessW(
		NULL,
		szCmdLine,
		NULL,
		NULL,
		FALSE,
		0,
		NULL,
		NULL,
		&si,
		&pi);

	if (!ret)
	{
		// エラー処理
		return -1;
	}

	// 新プロセスのハンドルは不要なので閉じる
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return 0;
}

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_USER_TRAYICON:
		switch (lParam)
		{
		case WM_RBUTTONUP:
		{
			POINT pt;
			GetCursorPos(&pt);

			HMENU hMenu = CreatePopupMenu();
			AppendMenu(hMenu, MF_STRING, ID_TRAY_MIDIPANIC, TEXT("MIDI Panic"));
			AppendMenu(hMenu, MF_STRING, ID_TRAY_ABOUT, TEXT("About..."));
			AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
			AppendMenu(hMenu, MF_STRING, ID_TRAY_RESTART, TEXT("Restart"));
			AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, TEXT("Exit"));

			// これを呼ばないとメニューが即消える
			SetForegroundWindow(hwnd);

			TrackPopupMenu(
				hMenu,
				TPM_RIGHTBUTTON,
				pt.x, pt.y,
				0,
				hwnd,
				nullptr);

			DestroyMenu(hMenu);
			break;
		}
		}
		return 0;

	case WM_USER_UPDATETRAYICON:
		UpdateTrayIcon(g_hWnd);
		break;

	case WM_DPICHANGED:
		UpdateTrayIcon(g_hWnd);
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_TRAY_MIDIPANIC:
			if (g_player) {
				g_player->resetOPL();
			}
			return 0;
		case ID_TRAY_ABOUT:
			MessageBox(hwnd, TEXT("ymfmidi for Windows v" VERSION " - " __DATE__ "\nThis softwerare includes components licensed under the BSD License. See LICENSE.txt for details."), TEXT("About"), MB_OK | MB_ICONINFORMATION);
			return 0;
		case ID_TRAY_EXIT:
			g_running = false;
			PostQuitMessage(0);
			return 0;
		case ID_TRAY_RESTART:
			g_running = false;
			PostQuitMessage(0);
			RestartApplication();
			return 0;
		}
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		if (msg == g_WM_TASKBARCREATED) {
			RegisterTrayIcon(g_hWnd);
			UpdateTrayIcon(g_hWnd);
		}
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

HWND CreateHiddenWindow(HINSTANCE hInst)
{
	WNDCLASS wc{};
	wc.lpfnWndProc = TrayWndProc;
	wc.hInstance = hInst;
	wc.lpszClassName = TEXT("ymfmidiwin_tray");

	g_WM_TASKBARCREATED = RegisterWindowMessage(L"TaskbarCreated");

	RegisterClass(&wc);

	return CreateWindow(
		wc.lpszClassName,
		TEXT(""),
		WS_OVERLAPPEDWINDOW,
		0, 0, 0, 0,
		nullptr,
		nullptr,
		hInst,
		nullptr);
}

typedef BOOL(WINAPI* PFN_SetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
void EnableHighDpiScaling()
{
	HMODULE hUser32 = LoadLibraryW(L"user32.dll");
	if (hUser32)
	{
		auto p = (PFN_SetThreadDpiAwarenessContext)
			GetProcAddress(hUser32, "SetThreadDpiAwarenessContext");
		if (p)
		{
			p((DPI_AWARENESS_CONTEXT)-4);
		}
		FreeLibrary(hUser32);
	}
}

// ----------------------------------------------------------------------------
int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	
	bool interactive = true;
	bool traymode = false;
	
	const char* songPath;
	const char* patchPath = "GENMIDI.wopl";
	const char* wavPath = nullptr;
	int sampleRate = 44100;
	int bufferSize = 4096;
	double gain = 1.0;
	double filter = 5.0;
	OPLPlayer::ChipType chipType = OPLPlayer::ChipOPL3;
	int numChips = 1;
	unsigned songNum = 0;
	bool stereo = true;
	int suspendTimeMilliseconds = 15000; // 15秒でサスペンド

	EnableHighDpiScaling();

	printf("ymfmidi for Windows v" VERSION " - " __DATE__ "\n");

	char opt;
	int optionindex = 0;
	while ((opt = getopt_long(argc, argv, ":hq1s:o:c:n:mb:g:r:f:tp:", options, &optionindex)) != -1)
	{
		switch (opt)
		{
		case ':':
		case 'h':
			usage();
			break;
		
		case 'q':
			interactive = false;
			break;
		
		case '1':
			g_looping = false;
			break;
		
		case 's':
			songNum = atoi(optarg);
			break;
		
		case 'o':
			wavPath = optarg;
			g_looping = false;
			break;
		
		case 'c':
			switch (atoi(optarg))
			{
			case 1: chipType = OPLPlayer::ChipOPL; break;
			case 2: chipType = OPLPlayer::ChipOPL2; break;
			case 3: chipType = OPLPlayer::ChipOPL3; break;
			default:
				fprintf(stderr, "invalid chip type\n");
				exit(1);
			}
			break;
		
		case 'n':
			numChips = atoi(optarg);
			if (numChips < 1)
			{
				fprintf(stderr, "number of chips must be at least 1\n");
				exit(1);
			}
			break;
		
		case 'm':
			stereo = false;
			break;
		
		case 'b':
			bufferSize = atoi(optarg);
			if (!bufferSize)
			{
				fprintf(stderr, "invalid buffer size: %s\n", optarg);
				exit(1);
			}
			break;
		
		case 'g':
			gain = atof(optarg);
			if (!gain)
			{
				fprintf(stderr, "invalid gain: %s\n", optarg);
				exit(1);
			}
			break;
		
		case 'r':
			sampleRate = atoi(optarg);
			if (!sampleRate)
			{
				fprintf(stderr, "invalid sample rate: %s\n", optarg);
				exit(1);
			}
			break;
		
		case 'f':
			filter = atof(optarg);
			if (filter < 0.0)
			{
				fprintf(stderr, "invalid cutoff: %s\n", optarg);
				exit(1);
			}
			break;

		case 't':
			// タスクトレイ常駐モード
			traymode = true;
			break;

		case 'p':
			// サスペンド時間
			suspendTimeMilliseconds = atoi(optarg);
			break;

		case 0:
			if (strcmp(options[optionindex].name, "resampler") == 0) {
				// サンプリング変換タイプ
				if (strcmp(optarg, "linear") == 0) {
					g_srconvtype = SRC_LINEAR;
				}
				else if (strcmp(optarg, "nearest") == 0) {
					g_srconvtype = SRC_ZERO_ORDER_HOLD;
				}
				else if (strcmp(optarg, "sinc") == 0 || strcmp(optarg, "sinc_fast") == 0) {
					g_srconvtype = SRC_SINC_FASTEST;
				}
				else if (strcmp(optarg, "sinc_medium") == 0) {
					g_srconvtype = SRC_SINC_MEDIUM_QUALITY;
				}
				else if (strcmp(optarg, "sinc_best") == 0) {
					g_srconvtype = SRC_SINC_BEST_QUALITY;
				}
			}
			else if (strcmp(options[optionindex].name, "tail-time") == 0) {
				// WAV出力時の末尾のマージン時間
				if (strcmp(optarg, "auto") == 0) {
					g_wavOutputMarginAuto = true;
				}
				else {
					g_wavOutputMarginAuto = false;
					g_wavOutputMarginMillisecond = atoi(optarg);
					if (g_wavOutputMarginMillisecond < 0) g_wavOutputMarginMillisecond = 0;
				}
			}
			break;
		}
	}
	
	if (optind >= argc)
		usage();

	songPath = argv[optind];
	if (optind + 1 < argc)
		patchPath = argv[optind + 1];
	{
		const char* fileext = strrchr(songPath, '.');
		if (fileext && (_stricmp(fileext, ".wopl") == 0 || _stricmp(fileext, ".opl") == 0 || _stricmp(fileext, ".op2") == 0 || _stricmp(fileext, ".tmb") == 0 || _stricmp(fileext, ".ad") == 0)) {
			const char* tmp = patchPath;
			patchPath = songPath;
			songPath = tmp;
		}
	}
	
	auto player = new OPLPlayer(numChips, chipType);
	
	if (!player->loadSequence(songPath))
	{
		fprintf(stderr, "couldn't load %s\n", songPath);
		exit(1);
	}
	
	if (!player->loadPatches(patchPath))
	{
		// exeと同じ場所にあればそれを使う
		std::string path = GetExeDirectory();
		path += "\\GENMIDI.wopl";
		if (!player->loadPatches(path.c_str()))
		{
			fprintf(stderr, "couldn't load %s\n", patchPath);
			exit(1);
		}
	}
	
	player->setLoop(g_looping);
	player->setSampleRate(sampleRate);
	player->setGain(gain);
	player->setFilter(filter);
	player->setStereo(stereo);
	if (songNum > 0)
		player->setSongNum(songNum - 1);
	player->setAutoSuspend(suspendTimeMilliseconds);
	
	if (interactive)
	{
		consoleOpen();
		consolePos(0);
		printf("song: %-32.32s | patches: %-29.29s\n",
			shortPath(songPath), shortPath(patchPath));
	}
	else
	{
		printf("song:    %s\npatches: %s\n",
			shortPath(songPath), shortPath(patchPath));
	}

	signal(SIGINT, quit);

	if (wavPath) 
	{
		if (memcmp(songPath, "//", 2) != 0) {
			char wavPathTmp[MAX_PATH] = { 0 };
			if (strcmp(wavPath, ".") == 0) {
				// 自動で名前を付ける
				strcpy_s(wavPathTmp, songPath);
				char* sepa = strrchr(wavPathTmp, '\\');
				char* ext = strrchr(wavPathTmp, '.');
				if (ext && ext > sepa) {
					*ext = '\0';
				}
				strcat_s(wavPathTmp, "_ymfm.wav");
				wavPath = wavPathTmp;
			}
			mainLoopWAV(player, wavPath, interactive);
		}
		else {
			fprintf(stderr, "WAV output is not possible when using MIDI IN\n");
		}
	}
	else
	{
		SetConsoleCtrlHandler(ConsoleHandler, TRUE);
#ifdef USE_SDL
		mainLoopSDL(player, bufferSize, interactive);
#else
		mainLoopWASAPI(player, bufferSize, interactive, traymode);
#endif
	}

	delete player;

	return 0;
}

// ----------------------------------------------------------------------------
#ifdef USE_SDL
static SDL_AudioSpec g_audioSpec;

static void audioCallback(void *data, uint8_t *stream, int len)
{
	memset(stream, g_audioSpec.silence, len);
	
	auto player = reinterpret_cast<OPLPlayer*>(data);
	if (g_audioSpec.format == AUDIO_F32SYS)
		player->generate(reinterpret_cast<float*>(stream), len / (2 * sizeof(float)));
	else if (g_audioSpec.format == AUDIO_S16SYS)
		player->generate(reinterpret_cast<int16_t*>(stream), len / (2 * sizeof(int16_t)));
	
	if (!g_looping)
		g_running &= !player->atEnd();
}

// ----------------------------------------------------------------------------
static void mainLoopSDL(OPLPlayer *player, int bufferSize, bool interactive)
{
	// init SDL audio now
	SDL_SetMainReady();
	SDL_Init(SDL_INIT_AUDIO);
	
	SDL_AudioSpec spec = {0};
	spec.freq     = player->sampleRate();
	spec.format   = AUDIO_F32SYS;
	spec.channels = 2;
	spec.samples  = bufferSize;
	spec.callback = audioCallback;
	spec.userdata = player;
	
	if (SDL_OpenAudio(&spec, &g_audioSpec))
	{
		fprintf(stderr, "couldn't open audio device\n");
		exit(1);
	}
	else if (g_audioSpec.format != AUDIO_F32SYS && g_audioSpec.format != AUDIO_S16SYS)
	{
		fprintf(stderr, "unsupported audio format (0x%x)\n", g_audioSpec.format);
		exit(1);
	}
	
	player->setSampleRate(g_audioSpec.freq);
	
	if (interactive)
	{
		consolePos(2);
		printf("\ncontrols: [p] pause, [r] restart, [tab] change view, [esc/q] quit\n");
	}

	SDL_PauseAudio(0);
	
	unsigned displayType = 0;
	while (g_running)
	{
		if (interactive)
		{
			if (player->numSongs() > 1)
			{
				consolePos(1);
				printf("part %3u/%-3u (use left/right to change)\n",
					player->songNum() + 1, player->numSongs());
			}
			
			consolePos(5);
			if (!displayType)
				player->displayChannels();
			else
				player->displayVoices();
			
			switch (consoleGetKey())
			{
			case 0x1b:
			case 'q':
				quit(0);
				continue;
			
			case 'p':
				g_paused ^= true;
				SDL_PauseAudio(g_paused);
				break;
			
			case 'r':
				g_paused = false;
				SDL_PauseAudio(0);
				player->reset();
				break;
				
			case 0x09:
				displayType ^= 1;
				consolePos(5);
				player->displayClear();
				break;
			
			case -'D':
				if (player->songNum() > 0)
					player->setSongNum(player->songNum() - 1);
				break;

			case -'C':
				if (player->songNum() < player->numSongs() - 1)
					player->setSongNum(player->songNum() + 1);
				break;
			}
		}
		SDL_Delay(30);
	}

	SDL_Quit();
}
#endif

// ----------------------------------------------------------------------------
static void mainLoopWAV(OPLPlayer *player, const char *path, bool interactive)
{
	FILE* wav;
	if (fopen_s(&wav, path, "wb"))
	{
		fprintf(stderr, "couldn't open %s\n", path);
		exit(1);
	}
	
	printf("rendering %s...\n", path);
	
	fseek(wav, 44, SEEK_SET);
	
	uint32_t numSamples = 0;
	const int nChannels = player->stereo() ? 2 : 1;
	const unsigned bytesPerSample = nChannels * 2;
	const uint32_t sampleRate = player->sampleRate();
	const int displayStep = sampleRate / 10;

	// --- libsamplerate ---
	int err = 0;
	SRC_STATE* src = src_new(
		g_srconvtype,
		player->stereo() ? 2 : 1,
		&err);

	double ratio = (double)sampleRate / INTERNAL_SR;

	player->setSampleRate(INTERNAL_SR); // OPL original rate

	const int inBufferSamples = 4096;
	const int outBufferSamples = inBufferSamples * ratio + 64; // マージン付けておく
	std::vector<float> in(inBufferSamples * nChannels);
	std::vector<float> out(outBufferSamples * nChannels);
	std::vector<uint16_t> out16(outBufferSamples * nChannels);

	// サンプリング変換改善のために無音データを放り込んでおく
	{
		SRC_DATA d{};
		for (int i = 0; i < inBufferSamples; i++) {
			in[i] = 0;
		}
		d.data_in = in.data();
		d.input_frames = inBufferSamples;
		d.data_out = out.data();
		d.output_frames = outBufferSamples;
		d.src_ratio = ratio;
	}

	const int outwavMaxAmpitude = 32767;
	const float noSoundThreshold = 1.0f / outwavMaxAmpitude; // 無音と見做す音量
	int lastDispPos = 0;
	int extendSamples = g_wavOutputMarginAuto ? (INTERNAL_SR * 5) : (int)((int64_t)g_wavOutputMarginMillisecond * INTERNAL_SR / 1000); // 指定した時間のばす。自動で末尾を探すのは5秒以内
	int zeroCounter = 0;
	bool endOutput = false;
	while ((!player->atEnd() || extendSamples > 0) && g_running && !endOutput)
	{
		uint32_t inBufferCount = 0;
		for (int i = 0; i < inBufferSamples; i++) {
			in[i * 2] = in[i * 2 + 1] = 0;
			float* data = in.data() + i * 2;
			player->generate(data, 1);
			inBufferCount++;
			if (player->atEnd() || !g_running) {
				if (extendSamples > 0) {
					extendSamples--;
				}
				else {
					endOutput = true;
					break;
				}
				if (-noSoundThreshold <= data[0] && data[0] <= noSoundThreshold &&
					-noSoundThreshold <= data[1] && data[1] <= noSoundThreshold) {
					zeroCounter++;
				}
				else {
					zeroCounter = 0;
				}
				if (g_wavOutputMarginAuto && zeroCounter >= INTERNAL_SR / 10) {
					// INTERNAL_SR / 10 サンプル = 0.1秒無音なら終了と見做す
					endOutput = true;
					break;
				}
			}
		}

		// --- SR 変換 ---
		SRC_DATA d{};
		d.data_in = in.data();
		d.input_frames = min(inBufferSamples, inBufferCount);
		d.data_out = out.data();
		d.output_frames = outBufferSamples;
		d.src_ratio = ratio;

		src_process(src, &d);

		const int gensamples = d.output_frames_gen;
		const int count = d.output_frames_gen * nChannels;
		for (int i = 0; i < count; i++) {
			float f1 = out[i] * outwavMaxAmpitude;
			if (f1 < -outwavMaxAmpitude) f1 = -outwavMaxAmpitude;
			if (f1 > +outwavMaxAmpitude) f1 = +outwavMaxAmpitude;
			out16[i] = (uint16_t)round(f1);
		}

		if (fwrite(out16.data(), bytesPerSample, gensamples, wav) != gensamples)
		{
			fprintf(stderr, "writing WAV data failed\n");
			exit(1);
		}
		numSamples += gensamples;

		int curDispPos = numSamples / sampleRate;
		if (interactive && curDispPos != lastDispPos) {
			consolePos(4);
			printf("Time: %d sec\n", curDispPos);
			lastDispPos = curDispPos;

			printf("\ncontrols: [esc/q] quit\n");

			switch (consoleGetKey())
			{
			case 0x1b:
			case 'q':
				g_running = false;
				continue;
			}
		}
	}
	
	// fill in the rendered sample size and write the header
	const uint32_t byteRate = sampleRate * bytesPerSample;
	const uint32_t dataSize = numSamples * bytesPerSample;
	const uint32_t wavSize = dataSize + 36;
	
	char header[44] = {0};
	
	header[0] = 'R';
	header[1] = 'I';
	header[2] = 'F';
	header[3] = 'F';
	header[4] = (char)(wavSize);
	header[5] = (char)(wavSize >> 8);
	header[6] = (char)(wavSize >> 16);
	header[7] = (char)(wavSize >> 24);
	header[8]  = 'W';
	header[9]  = 'A';
	header[10] = 'V';
	header[11] = 'E';
	
	// format chunk
	header[12] = 'f';
	header[13] = 'm';
	header[14] = 't';
	header[15] = ' ';
	header[16] = 16; // chunk size
	header[20] = 1;  // sample format (PCM)
	if (bytesPerSample == 4)
		header[22] = 2;  // stereo
	else
		header[22] = 1;  // mono
	header[24] = (char)(sampleRate);
	header[25] = (char)(sampleRate >> 8);
	header[26] = (char)(sampleRate >> 16);
	header[27] = (char)(sampleRate >> 24);
	header[28] = (char)(byteRate);
	header[29] = (char)(byteRate >> 8);
	header[30] = (char)(byteRate >> 16);
	header[31] = (char)(byteRate >> 24);
	header[32] = 4;  // bytes per sample
	header[34] = 16; // bits per sample
	
	// data chunk
	header[36] = 'd';
	header[37] = 'a';
	header[38] = 't';
	header[39] = 'a';
	header[40] = (char)(dataSize);
	header[41] = (char)(dataSize >> 8);
	header[42] = (char)(dataSize >> 16);
	header[43] = (char)(dataSize >> 24);
	
	fseek(wav, 0, SEEK_SET);
	if (fwrite(header, 1, sizeof(header), wav) != sizeof(header))
	{
		fprintf(stderr, "writing WAV header failed\n");
		exit(1);
	}
	
	fclose(wav);
}

#ifndef USE_SDL
class DeviceNotificationClient : public IMMNotificationClient
{
	LONG _ref = 1;

public:
	// IUnknown
	ULONG STDMETHODCALLTYPE AddRef() override {
		return InterlockedIncrement(&_ref);
	}
	ULONG STDMETHODCALLTYPE Release() override {
		ULONG r = InterlockedDecrement(&_ref);
		if (r == 0) delete this;
		return r;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
		if (riid == __uuidof(IUnknown) ||
			riid == __uuidof(IMMNotificationClient)) {
			*ppv = this;
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}

	// 既定デバイス変更
	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
		EDataFlow flow,
		ERole role,
		LPCWSTR pwstrDeviceId) override
	{
		if (flow == eRender && role == eConsole) {
			g_restart = true;
		}
		return S_OK;
	}

	// 未使用でも実装必須
	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};

// ----------------------------------------------------------------------------
void StartWasapiAudio(OPLPlayer *player)
{
	// --- WASAPI 初期化 ---
	IMMDeviceEnumerator* enumerator = nullptr;
	IMMDevice* device = nullptr;
	IAudioClient* audioClient = nullptr;
	IAudioRenderClient* renderClient = nullptr;
	WAVEFORMATEX* mixFmt = nullptr;
	SRC_STATE* srconv = nullptr;
	DeviceNotificationClient* notify = new DeviceNotificationClient();

	bool notifyValid = false;

	HANDLE hAudioEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (hAudioEvent == NULL) {
		return;
	}

	HRESULT hr = S_OK;

	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
			CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
	if (FAILED(hr)) {
		fprintf(stderr, "MMDeviceEnumerator CoCreateInstance failed\n");
		goto finalize;
	}

	hr = enumerator->RegisterEndpointNotificationCallback(notify);
	if (SUCCEEDED(hr)) {
		notifyValid = true;
	}
	else {
		notify->Release();
	}

	hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
	if (FAILED(hr)) {
		Sleep(1000);
		g_restart = true;
		goto finalize;
	}

	hr = device->Activate(__uuidof(IAudioClient),
		CLSCTX_ALL, nullptr, (void**)&audioClient);
	if (FAILED(hr)) {
		Sleep(1000);
		g_restart = true;
		goto finalize;
	}

	hr = audioClient->GetMixFormat(&mixFmt);
	if (FAILED(hr)) {
		Sleep(1000);
		g_restart = true;
		goto finalize;
	}

	hr = audioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
		2000000,
		0,
		mixFmt,
		nullptr);
	if (FAILED(hr)) {
		Sleep(1000);
		g_restart = true;
		goto finalize;
	}
	hr = audioClient->GetService(IID_PPV_ARGS(&renderClient));
	if (FAILED(hr)) {
		Sleep(1000);
		g_restart = true;
		goto finalize;
	}
	else {
		UINT32 bufferFrames = 0;
		audioClient->GetBufferSize(&bufferFrames);

		auto* ext = (WAVEFORMATEXTENSIBLE*)mixFmt;
		if (ext->SubFormat != KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
		{
			goto finalize;
		}

		// --- libsamplerate ---
		int err = 0;
		srconv = src_new(g_srconvtype, mixFmt->nChannels, &err);
		if (!srconv) {
			goto finalize;
		}
		else {
			double ratio = (double)mixFmt->nSamplesPerSec / INTERNAL_SR;

			// --- FIFO ---
			std::vector<float> fifo;
			const int fifosamples = bufferFrames;
			fifo.reserve(fifosamples * mixFmt->nChannels);

			const int outBufferSamples = fifosamples + 64;
			const int inBufferSamples = (int)(fifosamples / ratio);
			std::vector<float> in(inBufferSamples * mixFmt->nChannels);
			std::vector<float> out(outBufferSamples * mixFmt->nChannels);

			hr = audioClient->SetEventHandle(hAudioEvent);
			if (FAILED(hr)) {
				Sleep(1000);
				g_restart = true;
				goto finalize;
			}

			hr = audioClient->Start();
			if (FAILED(hr)) {
				Sleep(1000);
				g_restart = true;
				goto finalize;
			}

			while (g_running && !g_restart)
			{
				if (g_paused)
				{
					Sleep(100);
					continue;
				}

				UINT32 padding = 0;
				hr = audioClient->GetCurrentPadding(&padding);
				if (FAILED(hr)) {
					Sleep(1000);
					g_restart = true;
					goto finalize;
				}

				UINT32 framesAvailable = bufferFrames - padding;

				int sampleremain = fifosamples - fifo.size();
				if (sampleremain > 0)
				{
					// --- 波形生成 ---
					int samples = min(inBufferSamples, sampleremain);
					player->generate(reinterpret_cast<float*>(in.data()), samples);

					if (!g_looping)
						g_running &= !player->atEnd();

					if (!player->isSleepMode()) {
						if (g_sleeping) {
							g_sleeping = false;
							PostMessage(g_hWnd, WM_USER_UPDATETRAYICON, 0, 0);
						}

						// --- SR 変換 ---
						SRC_DATA d{};
						d.data_in = in.data();
						d.input_frames = samples;
						d.data_out = out.data();
						d.output_frames = outBufferSamples;
						d.src_ratio = ratio;

						src_process(srconv, &d);

						fifo.insert(
							fifo.end(),
							out.data(),
							out.data() + d.output_frames_gen * mixFmt->nChannels);
					}
					else {
						if (!g_sleeping) {
							g_sleeping = true;
							PostMessage(g_hWnd, WM_USER_UPDATETRAYICON, 0, 0);
						}
						Sleep(100);
						continue;
					}
				}

				if (WaitForSingleObject(hAudioEvent, 200) == WAIT_TIMEOUT) {
					continue;
				}

				// --- WASAPI 出力 ---
				UINT32 fifoFrames = (UINT32)(fifo.size() / mixFmt->nChannels);
				UINT32 framesToWrite = min(framesAvailable, fifoFrames);

				if (framesToWrite > 0)
				{
					BYTE* data = nullptr;
					hr = renderClient->GetBuffer(framesToWrite, &data);
					if (FAILED(hr)) {
						g_restart = true;
						goto finalize;
					}

					memcpy(data,
						fifo.data(),
						framesToWrite * mixFmt->nBlockAlign);

					hr = renderClient->ReleaseBuffer(framesToWrite, 0);
					if (FAILED(hr)) {
						g_restart = true;
						goto finalize;
					}

					fifo.erase(
						fifo.begin(),
						fifo.begin() + framesToWrite * mixFmt->nChannels);
				}
			}

			audioClient->Stop();
		}
	}

finalize:
	if (srconv) src_delete(srconv);
	srconv = nullptr;

	if (notifyValid) {
		enumerator->UnregisterEndpointNotificationCallback(notify);
		notify->Release();
	}

	if (renderClient) renderClient->Release();
	if (audioClient) audioClient->Release();
	if (device) device->Release();
	if (enumerator) enumerator->Release();
	renderClient = nullptr;
	audioClient = nullptr;
	device = nullptr;
	enumerator = nullptr;

	if (mixFmt) CoTaskMemFree(mixFmt);
	mixFmt = nullptr;

	CloseHandle(hAudioEvent);
}
void AudioThread()
{
	auto player = g_player;
	if (!player) return;

	if (FAILED(CoInitialize(nullptr))) return;

	do {
		g_restart = false;
		StartWasapiAudio(player);
	} while (g_running && g_restart);

	g_running = false;

	CoUninitialize();
}

static void mainLoopWASAPI(OPLPlayer* player, int bufferSize, bool interactive, bool traymode)
{
	HWND hwnd = NULL;

	g_player = player;

	std::thread audio(AudioThread);

	player->setSampleRate(INTERNAL_SR); // OPL original rate

	if (traymode) {
		g_hInst = GetModuleHandle(nullptr);

		g_hWnd = hwnd = CreateHiddenWindow(g_hInst);
		if (!hwnd)
			return;

		ShowWindow(hwnd, SW_HIDE);
		if (RegisterTrayIcon(hwnd)) {
			// コンソールを消す
			FreeConsole();
		}
		else {
			traymode = false;
		}
	}
	if (interactive && !traymode)
	{
		consolePos(2);
		printf("\ncontrols: [p] pause, [r] restart, [tab] change view, [esc/q] quit\n");
	}

	unsigned displayType = 0;
	while (g_running)
	{
		if (traymode) {
			MSG msg;
			while (GetMessage(&msg, nullptr, 0, 0))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		else {
			if (interactive)
			{
				if (player->numSongs() > 1)
				{
					consolePos(1);
					printf("part %3u/%-3u (use left/right to change)\n",
						player->songNum() + 1, player->numSongs());
				}

				consolePos(5);
				if (!displayType)
					player->displayChannels();
				else
					player->displayVoices();

				switch (consoleGetKey())
				{
				case 0x1b:
				case 'q':
					quit(0);
					continue;

				case 'p':
					g_paused ^= true;
					break;

				case 'r':
					g_paused = false;
					player->reset();
					break;

				case 0x09:
					displayType ^= 1;
					consolePos(5);
					player->displayClear();
					break;

				case -'D':
					if (player->songNum() > 0)
						player->setSongNum(player->songNum() - 1);
					break;

				case -'C':
					if (player->songNum() < player->numSongs() - 1)
						player->setSongNum(player->songNum() + 1);
					break;
				}
			}
			Sleep(10);
		}
	}

	if (traymode) {
		g_hWnd = nullptr;

		// トレイ削除
		NOTIFYICONDATA nid{};
		nid.cbSize = sizeof(nid);
		nid.hWnd = hwnd;
		nid.uID = 1;
		Shell_NotifyIcon(NIM_DELETE, &nid);
		if (g_hIcon) {
			DestroyIcon(g_hIcon);
			g_hIcon = nullptr;
		}
		if (g_hIconSleep) {
			DestroyIcon(g_hIconSleep);
			g_hIconSleep = nullptr;
		}
		ReleaseOldTrayIcon();
	}
	g_running = false;
	audio.join();
}
#endif