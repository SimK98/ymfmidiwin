// 主要コードはshingo45endo氏のコードを移植して使用しています。
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
#include <vector>
#include <cstdint>
#include <optional>

// ユーザー定義SysEx
struct UserSysEx {
    std::vector<uint8_t> memo;
    std::vector<uint8_t> bytes;
};

// トラックデータ
struct RcmTrack {
    uint8_t trackNo = 0;
    int8_t midiCh = -1;
    int8_t chNo = -1;
    int8_t portNo = 0;
    uint8_t keyShift = 0;
    int8_t stShiftS = 0;  // signed ST+
    uint8_t stShiftU = 0; // unsigned ST+
    int8_t stShift = 0;   // 最終的に使うST+
    uint8_t mode = 0;
    std::vector<uint8_t> memo;
    std::vector<std::vector<uint8_t>> events;
    std::vector<std::vector<uint8_t>> extractedEvents;
    bool isCMU = false; // MCP用
};

// ヘッダ情報
struct RcmHeader {
    std::vector<uint8_t> title;
    std::vector<std::vector<uint8_t>> memoLines;
    uint16_t timeBase = 96;
    uint8_t tempo = 120;
    uint8_t beatN = 4;
    uint8_t beatD = 4;
    uint8_t key = 0;
    int8_t playBias = 0;

    std::vector<std::vector<uint8_t>> sysExsCM6;
    std::vector<std::vector<uint8_t>> sysExsGSD;
    std::vector<std::vector<uint8_t>> sysExsMTD;
    std::vector<std::vector<uint8_t>> sysExsGSD2;

    std::vector<std::vector<uint8_t>> patches; // MTD用のPatch Memory

    std::vector<UserSysEx> userSysExs; // 8個分

    bool isMCP = false;
    bool isF = false;
    bool isG = false;
    uint8_t maxTracks = 0;
};

// 全体構造
struct RcmData {
    RcmHeader header;
    std::vector<RcmTrack> tracks;
};


typedef struct MIDI_RCM {
	RcmHeader header;
	std::vector<RcmTrack> tracks;
};

typedef struct MIDI_RCMSEQ {

};

static std::unique_ptr<MIDI_RCM> parseRcp(const uint8_t* data, size_t size)
{
	return nullptr;
}
static std::unique_ptr<MIDI_RCM> parseG36(const uint8_t* data, size_t size)
{
	return nullptr;
}
static std::unique_ptr<MIDI_RCM> parseMcp(const uint8_t* data, size_t size)
{
	return nullptr;
}

static std::unique_ptr<MIDI_RCM> parseRcm(const uint8_t* data, size_t size)
{
	// Checks the arguments.
	if (!data || size == 0) {
		return nullptr;
	}

	std::unique_ptr<MIDI_RCM> rcm = parseRcp(data, size);
	if (!rcm) rcm = parseG36(data, size);
	if (!rcm) rcm = parseMcp(data, size);

	// Executes post-processing for each track.
	if (!rcm->header.isMCP) {
		// For RCP/G36
		for (RcmTrack& track : rcm->tracks) {
			// Sets MIDI channel No. and port No.
			track.chNo = (track.midiCh >= 0) ? track.midiCh % 16 : -1;
			track.portNo = (track.midiCh >= 0) ? (int8_t)std::trunc(track.midiCh / 16) : 0;

			//// Reinterprets ST+ if necessary.
			//if (!rcm->header.isF && !rcm->header.isG) {
			//	console.assert('stShiftS' in track && 'stShiftU' in track);
			//	if (settings.stPlus == = 'signed') {
			//		if (track.stShiftS != = track.stShift && (track.stShiftS < -99 || 99 < track.stShiftS)) {
			//			console.warn(`ST+ has been converted to signed as specified. (${ track.stShift } -> ${track.stShiftS}) But, it seems to be unsigned.`);
			//		}
			//		track.stShift = track.stShiftS;
			//	}
			//	else if (settings.stPlus == = 'unsigned') {
			//		track.stShift = track.stShiftU;
			//	}
			//}

			// Extracts same measures and loops.
			track.extractedEvents = extractEvents(track.events, rcm->header.timeBase, false, settings);
		}
	}
	else {
		// For MCP
		for (size_t i = 1; i < rcm->tracks.size() - 1; ++i) {
			auto& track = rcm->tracks[i];
			// Sets MIDI channel No.
			track.chNo = (track.midiCh >= 0) ? track.midiCh : -1;

			// Extracts loops.
			track.extractedEvents = extractEvents(track.events, rcm->header.timeBase, true, settings);
		}

		// Extracts rhythm track.
		//console.assert(rcm->tracks.length >= 10);
		RcmTrack& seqTrack = rcm->tracks[9];
		RcmTrack& patternTrack = rcm->tracks[0];

		seqTrack.chNo = seqTrack.midiCh;
		seqTrack.extractedEvents = extractRhythm(seqTrack.events, patternTrack.events, settings);
	}


	return rcm;
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