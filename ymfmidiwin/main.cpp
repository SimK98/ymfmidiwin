#include <cstdio>
#include <signal.h>
#include <windows.h>
#include <getopt.h>

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

#define INTERNAL_SR 50000

#include "console.h"
#include "player.h"
#include <thread>

#define VERSION "0.6.0"

static bool g_running = true;
static bool g_paused = false;
static bool g_looping = true;

static OPLPlayer *g_player = nullptr;

#ifdef USE_SDL
static void mainLoopSDL(OPLPlayer* player, int bufferSize, bool interactive);
#else
static void mainLoopWASAPI(OPLPlayer* player, int bufferSize, bool interactive);
#endif
static void mainLoopWAV(OPLPlayer* player, const char* path);

// ----------------------------------------------------------------------------
void usage()
{
	fprintf(stderr, 
	"usage: ymfmidi [options] song_path [patch_path]\n"
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

// ----------------------------------------------------------------------------
int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	
	bool interactive = true;
	
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

	printf("ymfmidi v" VERSION " - " __DATE__ "\n");

	char opt;
	while ((opt = getopt_long(argc, argv, ":hq1s:o:c:n:mb:g:r:f:", options, nullptr)) != -1)
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
			interactive = g_looping = false;
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
		}
	}
	
	if (optind >= argc)
		usage();
	
	songPath = argv[optind];
	if (optind + 1 < argc)
		patchPath = argv[optind + 1];
	
	auto player = new OPLPlayer(numChips, chipType);
	
	if (!player->loadSequence(songPath))
	{
		fprintf(stderr, "couldn't load %s\n", songPath);
		exit(1);
	}
	
	if (!player->loadPatches(patchPath))
	{
		// exe‚Æ“¯‚¶êŠ‚É‚ ‚ê‚Î‚»‚ê‚ðŽg‚¤
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
		mainLoopWAV(player, wavPath);
	}
	else
	{
#ifdef USE_SDL
		mainLoopSDL(player, bufferSize, interactive);
#else
		mainLoopWASAPI(player, bufferSize, interactive);
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
static void mainLoopWAV(OPLPlayer *player, const char *path)
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
	uint16_t samples[2];
	char outSamples[4];
	const unsigned bytesPerSample = player->stereo() ? 4 : 2;
	
	while (!player->atEnd())
	{
		player->generate(reinterpret_cast<int16_t*>(samples), 1);
		outSamples[0] = samples[0];
		outSamples[1] = samples[0] >> 8;
		outSamples[2] = samples[1];
		outSamples[3] = samples[1] >> 8;
		
		if (fwrite(outSamples, 1, bytesPerSample, wav) != bytesPerSample)
		{
			fprintf(stderr, "writing WAV data failed\n");
			exit(1);
		}
		numSamples++;
	}
	
	// fill in the rendered sample size and write the header
	const uint32_t sampleRate = player->sampleRate();
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
// ----------------------------------------------------------------------------
void AudioThread()
{
	auto player = g_player;
	if (!g_player) return;

	CoInitialize(nullptr);

	// --- WASAPI ‰Šú‰» ---
	IMMDeviceEnumerator* enumerator = nullptr;
	IMMDevice* device = nullptr;
	IAudioClient* audioClient = nullptr;
	IAudioRenderClient* renderClient = nullptr;
	WAVEFORMATEX* mixFmt = nullptr;

	CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
		CLSCTX_ALL, IID_PPV_ARGS(&enumerator));

	enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
	device->Activate(__uuidof(IAudioClient),
		CLSCTX_ALL, nullptr, (void**)&audioClient);

	audioClient->GetMixFormat(&mixFmt);

	audioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		0,
		10000000,
		0,
		mixFmt,
		nullptr);

	audioClient->GetService(IID_PPV_ARGS(&renderClient));

	UINT32 bufferFrames = 0;
	audioClient->GetBufferSize(&bufferFrames);

	auto* ext = (WAVEFORMATEXTENSIBLE*)mixFmt;
	if (ext->SubFormat != KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
	{
		CoTaskMemFree(mixFmt);
		CoUninitialize();
		return;
	}

	// --- libsamplerate ---
	int err = 0;
	SRC_STATE* src = src_new(
		SRC_SINC_FASTEST,
		mixFmt->nChannels,
		&err);

	double ratio =
		double(mixFmt->nSamplesPerSec) / 50000.0;

	// --- FIFO ---
	std::vector<float> fifo;
	fifo.reserve(mixFmt->nSamplesPerSec * mixFmt->nChannels);

	const int inBufferSamples = 512;
	const int outBufferSamples = (inBufferSamples * ratio) + 64;
	std::vector<float> in(inBufferSamples * mixFmt->nChannels);
	std::vector<float> out(outBufferSamples * mixFmt->nChannels);

	audioClient->Start();

	while (g_running)
	{
		if (g_paused)
		{
			Sleep(10);
			continue;
		}

		UINT32 padding = 0;
		audioClient->GetCurrentPadding(&padding);

		UINT32 framesAvailable = bufferFrames - padding;

		// --- ”gŒ`¶¬ ---
		player->generate(reinterpret_cast<float*>(in.data()), inBufferSamples);

		if (!g_looping)
			g_running &= !player->atEnd();

		// --- SR •ÏŠ· ---
		SRC_DATA d{};
		d.data_in = in.data();
		d.input_frames = inBufferSamples;
		d.data_out = out.data();
		d.output_frames = outBufferSamples;
		d.src_ratio = ratio;

		src_process(src, &d);

		fifo.insert(
			fifo.end(),
			out.data(),
			out.data() + d.output_frames_gen * mixFmt->nChannels);

		// --- WASAPI o—Í ---
		UINT32 fifoFrames =
			(UINT32)(fifo.size() / mixFmt->nChannels);

		UINT32 framesToWrite =
			min(framesAvailable, fifoFrames);

		if (framesToWrite > 0)
		{
			BYTE* data = nullptr;
			renderClient->GetBuffer(framesToWrite, &data);

			memcpy(data,
				fifo.data(),
				framesToWrite * mixFmt->nBlockAlign);

			renderClient->ReleaseBuffer(framesToWrite, 0);

			fifo.erase(
				fifo.begin(),
				fifo.begin() + framesToWrite * mixFmt->nChannels);
		}
		else
		{
			Sleep(1);
		}
	}

	audioClient->Stop();
	src_delete(src);

	CoTaskMemFree(mixFmt);
	CoUninitialize();

	g_running = false;
}

static void mainLoopWASAPI(OPLPlayer* player, int bufferSize, bool interactive)
{
	g_player = player;

	std::thread audio(AudioThread);

	player->setSampleRate(50000); // OPL original rate

	if (interactive)
	{
		consolePos(2);
		printf("\ncontrols: [p] pause, [r] restart, [tab] change view, [esc/q] quit\n");
	}

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
	g_running = 0;
	audio.join();
}
#endif