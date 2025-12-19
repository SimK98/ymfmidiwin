#include "sequence_midiin.h"

#include <cmath>
#include <cstring>

#define READ_U16BE(data, pos) ((data[pos] << 8) | data[pos+1])
#define READ_U24BE(data, pos) ((data[pos] << 16) | (data[pos+1] << 8) | data[pos+2])
#define READ_U32BE(data, pos) ((data[pos] << 24) | (data[pos+1] << 16) | (data[pos+2] << 8) | data[pos+3])

#if 0
#include <windows.h>
#undef	min
#undef	TRACEOUT
static void trace_fmt_ex(const char* fmt, ...)
{
	char stmp[2048];
	va_list ap;
	va_start(ap, fmt);
	vsprintf_s(stmp, fmt, ap);
	strcat_s(stmp, "\n");
	va_end(ap);
	OutputDebugStringA(stmp);
}
#define	TRACEOUT(s)	trace_fmt_ex s
static void trace_fmt_exw(const WCHAR* fmt, ...)
{
	WCHAR stmp[2048];
	va_list ap;
	va_start(ap, fmt);
	vswprintf_s(stmp, 2048, fmt, ap);
	wcscat_s(stmp, L"\n");
	va_end(ap);
	OutputDebugStringW(stmp);
}
#define	TRACEOUTW(s)	trace_fmt_exw s
#else
#define	TRACEOUT(s)	(void)0
#define	TRACEOUTW(s)	(void)0
#endif	/* 1 */


// ----------------------------------------------------------------------------
SequenceMIDIIN::SequenceMIDIIN() : 
	m_portnum(0),
	m_currentTime(0),
	m_currentTimeReal(0),
	m_lastEmpty(false),
	Sequence()
{
}
SequenceMIDIIN::SequenceMIDIIN(int portnum) :
	m_portnum(0),
	m_currentTime(0),
	m_currentTimeReal(0),
	m_lastEmpty(false),
	Sequence()
{
	m_portnum = portnum;
}

// ----------------------------------------------------------------------------
SequenceMIDIIN::~SequenceMIDIIN()
{
	// 終了時
	m_midiIn.close();
}

// ----------------------------------------------------------------------------
bool SequenceMIDIIN::isValid(const uint8_t* data, size_t size)
{
	if (size >= 9 && !memcmp(data, "//MIDIIN", 8))
	{
		return true;
	}
	return false;
}

// ----------------------------------------------------------------------------
void SequenceMIDIIN::read(const uint8_t* data, size_t size)
{
	if (size >= 9 && !memcmp(data, "//MIDIIN", 8))
	{
		m_portnum = atol((const char*)data + 8);
	}
}

// ----------------------------------------------------------------------------
void SequenceMIDIIN::reset()
{
	Sequence::reset();
	setDefaults();

    if (!m_midiIn.open(m_portnum))
    {
        MessageBox(nullptr, L"MIDI IN open failed", L"Error", MB_OK);
        return;
    }
	m_currentTime = 0;
	m_currentTimeReal = GetTickCount64();
}

// ----------------------------------------------------------------------------
void SequenceMIDIIN::setDefaults()
{
}

// ----------------------------------------------------------------------------
void SequenceMIDIIN::setTimePerBeat(uint32_t usec)
{
	// nothing to do
}

// ----------------------------------------------------------------------------
unsigned SequenceMIDIIN::numSongs() const
{
	return 1;
}

// ----------------------------------------------------------------------------
uint32_t SequenceMIDIIN::update(OPLPlayer& player)
{
	MidiMessage m;
	size_t count = m_midiIn.getMessageCount();
	uint32_t nextTimestamp = m_midiIn.getNextTimestamp();

	if (count == 0) { // 空っぽなら待機モード 
		ULONGLONG curTimeReal = GetTickCount64();
		if (curTimeReal - m_currentTimeReal > 30000) {
			// スリープモード
			return UINT_MAX;
		}
		else if (curTimeReal - m_currentTimeReal > 10000) {
			// 10秒来なければ100msecくらいずれてもいいでしょう
			m_lastEmpty = true;
			return player.sampleRate() / 10;
		}
		else if (curTimeReal - m_currentTimeReal > 1000) {
			// 1秒来なければ10msecくらいずれてもいいでしょう
			return player.sampleRate() / 100;
		}
		else {
			// 次が来るかも知れないので最短待機
			return 1;
		}
	}
	else {
		MidiMessage m;
		m_midiIn.fetchOneMessage(m);
		BYTE status;
		BYTE data[2];
		status = m.data & 0xFF;
		data[0] = (m.data >> 8) & 0xFF;
		data[1] = (m.data >> 16) & 0xFF;
		switch (status >> 4)
		{
		case 9: // note on
			player.midiEvent(status, data[0], data[1]);
			break;

		case 8:  // note off
		case 10: // polyphonic pressure
		case 11: // controller change
		case 14: // pitch bend
			player.midiEvent(status, data[0], data[1]);
			break;

		case 12: // program change
		case 13: // channel pressure (ignored)
			player.midiEvent(status, data[0]);
			break;

		case 15: // sysex / meta event
		{
			MidiSysEx sysex;
			if (m_midiIn.fetchSysEx(sysex)) {
				if (!metaEvent(player, sysex)) {
					// end 
				}
			}

			break;
		}
		}

		m_currentTime = m.timestamp;
		m_currentTimeReal = GetTickCount64();
		if (nextTimestamp != UINT_MAX) {
			// 次のタイミングを返す
			DWORD nextTime = nextTimestamp - m_currentTime;
			return (uint32_t)((uint64_t)player.sampleRate() * nextTime / 1000);
		}
		else {
			// 次が来るかも知れないので最短待機
			return 1;
		}
	}
}

// ----------------------------------------------------------------------------
uint32_t SequenceMIDIIN::readVLQ(BYTE* exdata, int &pos, int exdatasize)
{
	uint32_t vlq = 0;
	uint8_t data = 0;

	do
	{
		data = exdata[pos++];
		vlq <<= 7;
		vlq |= (data & 0x7f);
	} while ((data & 0x80) && (pos < exdatasize));

	return vlq;
}

// ----------------------------------------------------------------------------
bool SequenceMIDIIN::metaEvent(OPLPlayer& player, MidiSysEx sysex)
{
	uint32_t len;

	BYTE exstatus = sysex.data[0];
	BYTE *exdata = sysex.data.data() + 1;
	int exdatasize = sysex.data.size() - 1;
	BYTE gmReset[] = { 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
	BYTE gsReset[] = { 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };

	//if (exstatus == 0xF0 && exdatasize == sizeof(gmReset) &&
	//	memcmp(exdata, gmReset, sizeof(gmReset)) == 0) 
	//{
	//	// GM Reset
	//	player.reset();
	//}
	//if (exstatus == 0xF0 && exdatasize == sizeof(gsReset) &&
	//	memcmp(exdata, gsReset, sizeof(gsReset)) == 0) 
	//{
	//	// GS Reset
	//	player.reset();
	//}

	int pos = 0;
	if (exstatus != 0xFF)
	{
		len = readVLQ(exdata, pos, exdatasize);
		if (exstatus == 0xf0)
			player.midiSysEx(exdata, exdatasize);
	}
	else
	{
		uint8_t data = exdata[pos++];
		len = readVLQ(exdata, pos, exdatasize);

		// end-of-track marker (or data just ran out)
		if (data == 0x2F)
		{
			return false;
		}
	}
	return true;
}
