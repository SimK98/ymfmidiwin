#pragma once

#include "sequence.h"
#include "sequence_mid.h"

class SequenceRCM : public SequenceMID
{
public:
	SequenceRCM() : SequenceMID() {

	}
	~SequenceRCM() override { }

	static bool isValid(const uint8_t* data, size_t size);

protected:

private:
	void read(const uint8_t* data, size_t size) override;
};
