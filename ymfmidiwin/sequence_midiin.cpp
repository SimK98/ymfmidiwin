#include "sequence_midiin.h"

#include <cmath>
#include <cstring>

#define READ_U16BE(data, pos) ((data[pos] << 8) | data[pos+1])
#define READ_U24BE(data, pos) ((data[pos] << 16) | (data[pos+1] << 8) | data[pos+2])
#define READ_U32BE(data, pos) ((data[pos] << 24) | (data[pos+1] << 16) | (data[pos+2] << 8) | data[pos+3])


// ----------------------------------------------------------------------------
SequenceMIDIIN::SequenceMIDIIN()
	: m_portnum(0), Sequence()
{
}
SequenceMIDIIN::SequenceMIDIIN(int portnum)
	: Sequence()
{
	m_portnum = portnum;
}

// ----------------------------------------------------------------------------
SequenceMIDIIN::~SequenceMIDIIN()
{
	// I—¹Žž
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
	std::vector<MidiMessage> messages;
	m_midiIn.fetchMessages(messages);

	for (auto& m : messages)
	{
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
	}

	return 1;
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
	int pos = 0;
	if (exstatus != 0xFF)
	{
		len = readVLQ(exdata, pos, exdatasize);
		if (pos + len < exdatasize)
		{
			if (exstatus == 0xf0)
				player.midiSysEx(exdata + pos, len);
		}
		else
		{
			return false;
		}
	}
	else
	{
		uint8_t data = exdata[pos++];
		len = readVLQ(exdata, pos, exdatasize);

		// end-of-track marker (or data just ran out)
		if (data == 0x2F || (pos + len >= exdatasize))
		{
			return false;
		}
	}
	return true;
}
