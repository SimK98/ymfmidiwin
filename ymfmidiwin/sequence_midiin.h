#ifndef __SEQUENCE_MIDIIN_H
#define __SEQUENCE_MIDIIN_H

#include "sequence.h"

#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <atomic>
#include <mutex>

#pragma comment(lib, "winmm.lib")

#if 0
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

struct MidiMessage
{
    DWORD data;      // status | data1 | data2
    DWORD timestamp; // ms
};

struct MidiSysEx
{
    std::vector<BYTE> data;
    DWORD timestamp; // ms
};

class MidiFifo
{
public:
    static constexpr size_t Capacity = 4096;

    void push(const MidiMessage& msg)
    {
        size_t w = writeIndex.load(std::memory_order_relaxed);
        buffer[w % Capacity] = msg;
        writeIndex.store(w + 1, std::memory_order_release);
    }

    size_t popAll(std::vector<MidiMessage>& out)
    {
        size_t r = readIndex.load(std::memory_order_relaxed);
        size_t w = writeIndex.load(std::memory_order_acquire);

        size_t count = w - r;
        if (count == 0) return 0;
        
        if (count > 10) TRACEOUT(("%d", count));
        out.reserve(out.size() + count);

        for (size_t i = 0; i < count; ++i)
            out.push_back(buffer[(r + i) % Capacity]);

        readIndex.store(w, std::memory_order_release);
        return count;
    }

    size_t popOne(MidiMessage& out)
    {
        size_t r = readIndex.load(std::memory_order_relaxed);
        size_t w = writeIndex.load(std::memory_order_acquire);

        size_t count = w - r;
        if (count == 0) return 0;

        out = buffer[r % Capacity];
        readIndex.store(r + 1, std::memory_order_release);

        return 1;
    }

    uint32_t getNextTimestamp()
    {
        size_t r = readIndex.load(std::memory_order_relaxed);
        size_t w = writeIndex.load(std::memory_order_acquire);

        size_t count = w - r;
        if (count == 0) return UINT_MAX;

        return buffer[r % Capacity].timestamp;
    }

    uint32_t getMessageCount()
    {
        size_t r = readIndex.load(std::memory_order_relaxed);
        size_t w = writeIndex.load(std::memory_order_acquire);

        return w - r;
    }

private:
    MidiMessage buffer[Capacity];
    std::atomic<size_t> writeIndex{ 0 };
    std::atomic<size_t> readIndex{ 0 };
};

class SysExFifo
{
public:
    void push(MidiSysEx&& msg)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(std::move(msg));
    }

    size_t pop(std::vector<MidiSysEx>& out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        out.insert(out.end(),
            std::make_move_iterator(m_queue.begin()),
            std::make_move_iterator(m_queue.end()));
        size_t n = m_queue.size();
        m_queue.clear();
        return n;
    }

    size_t popOne(MidiSysEx& out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_queue.empty())
            return 0;

        out = std::move(m_queue.front());
        m_queue.erase(m_queue.begin());
        return 1;
    }

private:
    std::mutex m_mutex;
    std::vector<MidiSysEx> m_queue;
};

class MidiInDevice
{
public:
    MidiInDevice() {
        m_hEventMidiInCallback = CreateEvent(NULL, FALSE, FALSE, NULL);
    }
    ~MidiInDevice() {
        close();
        if (m_hEventMidiInCallback) {
            CloseHandle(m_hEventMidiInCallback);
            m_hEventMidiInCallback = nullptr;
        }
    }

    bool open(int portnum)
    {
        if (m_hMidiIn) {
            close();
        }

        m_closing = false;
        MMRESULT r = midiInOpen(
            &m_hMidiIn,
            portnum,
            (DWORD_PTR)&MidiInCallback,
            (DWORD_PTR)this,
            CALLBACK_FUNCTION
        );

        if (r != MMSYSERR_NOERROR)
            return false;

        midiInStop(m_hMidiIn); // 動いているはずはないけど念のため
        midiInReset(m_hMidiIn);
        prepareSysExBuffers();
        midiInStart(m_hMidiIn);
        return true;
    }

    void close()
    {
        if (!m_hMidiIn)
            return;

        m_closing = true;
        midiInStop(m_hMidiIn);
        midiInReset(m_hMidiIn);
        freeSysExBuffers();
        midiInClose(m_hMidiIn);
        m_hMidiIn = nullptr;
    }

    // ----------------------------------------------------------
    // SysEx バッファ準備
    // ----------------------------------------------------------
    void prepareSysExBuffers()
    {
        for (int i = 0; i < SYSEX_BUFFER_COUNT; ++i)
        {
            MIDIHDR& hdr = m_sysexHdr[i];
            ZeroMemory(&hdr, sizeof(hdr));
            hdr.lpData = reinterpret_cast<LPSTR>(m_sysexData[i]);
            hdr.dwBufferLength = SYSEX_BUFFER_SIZE;

            midiInPrepareHeader(m_hMidiIn, &hdr, sizeof(hdr));
            midiInAddBuffer(m_hMidiIn, &hdr, sizeof(hdr));
        }
    }

    void freeSysExBuffers()
    {
        for (int i = 0; i < SYSEX_BUFFER_COUNT; ++i)
        {
            midiInUnprepareHeader(
                m_hMidiIn, &m_sysexHdr[i], sizeof(MIDIHDR));
        }
    }

    /// 呼び出した時点までのMIDIメッセージを取得
    size_t fetchMessages(std::vector<MidiMessage>& out)
    {
        return m_fifo.popAll(out);
    }
    size_t fetchOneMessage(MidiMessage& out)
    {
        return m_fifo.popOne(out);
    }
    uint32_t getNextTimestamp()
    {
        return m_fifo.getNextTimestamp();
    }
    uint32_t getMessageCount()
    {
        return m_fifo.getMessageCount();
    }
    size_t fetchSysEx(MidiSysEx& out)
    {
        return m_sysexFifo.popOne(out);
    }
    size_t fetchSysEx(std::vector<MidiSysEx>& out)
    {
        return m_sysexFifo.pop(out);
    }

    HANDLE getWakeupEvent()
    {
        return m_hEventMidiInCallback;
    }

private:
    static void CALLBACK MidiInCallback(
        HMIDIIN,
        UINT wMsg,
        DWORD_PTR dwInstance,
        DWORD_PTR dwParam1,
        DWORD_PTR dwParam2)
    {
        MidiInDevice* self = reinterpret_cast<MidiInDevice*>(dwInstance);

        if (self->m_closing) return;

        SetEvent(self->m_hEventMidiInCallback);

        switch (wMsg)
        {
        case MIM_DATA:
        {
            MidiMessage msg;
            msg.data = static_cast<DWORD>(dwParam1);
            msg.timestamp = static_cast<DWORD>(dwParam2);
            //TRACEOUT(("short:%08x", msg.data));
            self->m_fifo.push(msg);
            break;
        }
        case MIM_LONGDATA:
        {
            MIDIHDR* hdr = reinterpret_cast<MIDIHDR*>(dwParam1);

            if (hdr->dwBytesRecorded > 0)
            {
                MidiSysEx sx;
                sx.timestamp = static_cast<DWORD>(dwParam2);
                sx.data.assign(
                    reinterpret_cast<BYTE*>(hdr->lpData),
                    reinterpret_cast<BYTE*>(hdr->lpData)
                    + hdr->dwBytesRecorded);
                //TRACEOUT(("long:%02x", sx.data.at(0)));

                self->m_sysexFifo.push(std::move(sx));

                // sysexの位置が分かるように入れておく
                MidiMessage msg;
                msg.data = 0xf0;
                msg.timestamp = static_cast<DWORD>(dwParam2);
                self->m_fifo.push(msg);
            }

            // 再生継続なら再投入
            if (!self->m_closing) {
                hdr->dwBytesRecorded = 0;
                midiInAddBuffer(self->m_hMidiIn, hdr, sizeof(MIDIHDR));
            }
            break;
        }
        }
    }

private:
    static constexpr int SYSEX_BUFFER_SIZE = 1024;
    static constexpr int SYSEX_BUFFER_COUNT = 4;

    bool m_closing = false;

    HMIDIIN  m_hMidiIn = nullptr;
    MidiFifo m_fifo;
    SysExFifo m_sysexFifo;

    MIDIHDR m_sysexHdr[SYSEX_BUFFER_COUNT]{};
    BYTE    m_sysexData[SYSEX_BUFFER_COUNT][SYSEX_BUFFER_SIZE]{};

    HANDLE m_hEventMidiInCallback = nullptr;
};

class SequenceMIDIIN : public Sequence
{
public:
	SequenceMIDIIN();
	SequenceMIDIIN(int portnum);
	~SequenceMIDIIN();

	void reset();
	uint32_t update(OPLPlayer& player);

	virtual void setTimePerBeat(uint32_t usec);

	unsigned numSongs() const;

	static bool isValid(const uint8_t* data, size_t size);

    std::string GetFriendlyName();

    void* getWakeupEvent();

protected:
	uint32_t m_portnum;

private:
	void read(const uint8_t* data, size_t size);
	virtual void setDefaults();

    uint32_t readVLQ(BYTE* exdata, int& pos, int exdatasize);
    bool metaEvent(OPLPlayer& player, MidiSysEx sysex);

	MidiInDevice m_midiIn;

    DWORD  m_currentTime;
    ULONGLONG  m_currentTimeReal;
    bool  m_lastSleepMode;
};

#endif // __SEQUENCE_MIDIIN_H
