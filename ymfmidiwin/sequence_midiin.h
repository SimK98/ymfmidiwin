#ifndef __SEQUENCE_MIDIIN_H
#define __SEQUENCE_MIDIIN_H

#include "sequence.h"

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

protected:
	uint32_t m_portnum;

private:
	void read(const uint8_t* data, size_t size);
	virtual void setDefaults();
};

#endif // __SEQUENCE_MIDIIN_H
