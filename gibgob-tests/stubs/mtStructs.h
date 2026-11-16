#pragma once
#include <stdint.h>
static constexpr int kSampleTracks = 8;
static constexpr int kMidiTrackFirstIdx = 8;
static constexpr int kAllTracks = 16;
static constexpr uint8_t kSampleInstrumentsCount = 48;
static constexpr uint8_t kMidiInstrumentsCount = 16;
static constexpr uint8_t kSynthInstrumentsCount = 3;
static constexpr uint8_t kTotalInstrumentsCount = 67;
struct strInstrument { uint8_t isActive; };
struct strMtProject
{
    strInstrument instrument[kSampleInstrumentsCount];
    struct { uint8_t trackMute[8]; } values;
};
extern strMtProject mtProject;
extern const char mtNotes[128][5];
