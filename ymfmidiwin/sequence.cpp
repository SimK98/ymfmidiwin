#include <cstdio>

#include "sequence.h"
#include "sequence_hmi.h"
#include "sequence_hmp.h"
#include "sequence_mid.h"
#include "sequence_mus.h"
#include "sequence_xmi.h"
#include "sequence_midiin.h"

// ----------------------------------------------------------------------------
Sequence::~Sequence() {}

// ----------------------------------------------------------------------------
Sequence* Sequence::load(const char *path)
{
	if (_strnicmp(path, "//MIDIIN", 8) == 0) {
		Sequence* seqmidiin = new SequenceMIDIIN(atol(path + 8));
		if (seqmidiin)
		{
			seqmidiin->reset();
		}
		return seqmidiin;
	}

	FILE* file;
	if (fopen_s(&file, path, "rb")) return nullptr;
	
	Sequence *seq = load(file);
	
	fclose(file);
	return seq;
}

// ----------------------------------------------------------------------------
Sequence* Sequence::load(FILE *file, int offset, size_t size)
{
	if (!size)
	{
		fseek(file, 0, SEEK_END);
		if (ftell(file) < 0)
			return nullptr;
		size = ftell(file) - offset;
	}
	
	fseek(file, offset, SEEK_SET);
	std::vector<uint8_t> data(size);
	if (fread(data.data(), 1, size, file) != size)
		return nullptr;

	return load(data.data(), size);
}

// ----------------------------------------------------------------------------
Sequence* Sequence::load(const uint8_t *data, size_t size)
{
	Sequence *seq = nullptr;

	if (SequenceMIDIIN::isValid(data, size))
		seq = new SequenceMIDIIN();
	else if (SequenceMUS::isValid(data, size))
		seq = new SequenceMUS();
	else if (SequenceMID::isValid(data, size))
		seq = new SequenceMID();
	else if (SequenceXMI::isValid(data, size))
		seq = new SequenceXMI();
	else if (SequenceHMI::isValid(data, size))
		seq = new SequenceHMI();
	else if (SequenceHMP::isValid(data, size))
		seq = new SequenceHMP();
	
	if (seq)
	{
		seq->read(data, size);
		seq->reset();
	}
	
	return seq;
}
