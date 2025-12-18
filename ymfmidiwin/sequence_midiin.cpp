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
	return 1;
}
