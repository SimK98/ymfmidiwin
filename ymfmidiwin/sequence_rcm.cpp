// 一部のコードはshingo45endo氏のコードを使用しています。
// 
// MIT License
// 
// Copyright(c) 2019 - 2025 shingo45endo
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// 
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// 
// https://github.com/shingo45endo/rcm2smf

#include "sequence_rcm.h"

#include <cmath>
#include <cstring>

typedef struct MIDI_RCM {

};

typedef struct MIDI_RCMSEQ {

};

static std::unique_ptr<MIDI_RCM> parseRcm(const uint8_t* data, size_t size)
{
	return nullptr;
}

static std::unique_ptr<MIDI_RCMSEQ> convertRcmToSeq(const MIDI_RCM* rcm)
{
	return nullptr;
}

static std::vector<uint8_t> convertSeqToSmf(const MIDI_RCMSEQ* seq)
{
	return {};
}

// ----------------------------------------------------------------------------
bool SequenceRCM::isValid(const uint8_t* data, size_t size)
{
	std::unique_ptr<MIDI_RCM> rcm = parseRcm(data, size);
	
	return (rcm != nullptr);
}

// ----------------------------------------------------------------------------
void SequenceRCM::read(const uint8_t* data, size_t size)
{
	std::unique_ptr<MIDI_RCM> rcm = parseRcm(data, size);
	if (rcm) {
		std::unique_ptr<MIDI_RCMSEQ> seq = convertRcmToSeq(rcm.get());
		if (seq) {
			std::vector<uint8_t> smf = convertSeqToSmf(seq.get());
			if (smf.empty()) return;

			// 通常のMIDI化しているのでそれで読む
			SequenceRCM::read(smf.data(), size);
		}
	}
}