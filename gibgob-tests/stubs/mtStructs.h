#pragma once
#include <stdint.h>
static constexpr int kSampleTracks = 8;
static constexpr int kMidiTrackFirstIdx = 8;
static constexpr int kAllTracks = 16;
static constexpr uint8_t kSampleInstrumentsCount = 48;
static constexpr uint8_t kMidiInstrumentsCount = 16;
static constexpr uint8_t kSynthInstrumentsCount = 3;
static constexpr uint8_t kTotalInstrumentsCount = 67;
enum filterType { lowPass, highPass, bandPass };
static constexpr uint8_t MAX_INSTRUMENT_VOLUME = 100;
const uint8_t MAX_SLICE_NUMBER = 48;
constexpr uint8_t FIRST_BEAT_SLICE_NOTE = 24;
enum { playModeSingleShot = 0, playModeBeatSlice = 9 };
struct strInstrument { uint8_t isActive; uint8_t volume = 50; struct { char file_name[32]; } sample; float cutOff = 1.0f; uint8_t filterType = 0; uint8_t filterEnable = 0; uint8_t playMode = 0; uint8_t sliceNumber = 0; };
struct strMtProject
{
    strInstrument instrument[kSampleInstrumentsCount];
    struct { uint8_t trackMute[8]; char TrackNames[8][21]; uint8_t lastUsedInstrument; } values;
};
extern strMtProject mtProject;
extern const char mtNotes[128][5];
