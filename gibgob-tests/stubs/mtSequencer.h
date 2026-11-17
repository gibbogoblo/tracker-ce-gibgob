#pragma once
#include <stdint.h>
#include "mtStructs.h"
#define AUDIO_CORE_RAM
bool isSampleInstrument(int instrument);
bool isSynthInstrument(int instrument);
int instrumentIdxToMidiChannel(int instrument);
class Sequencer
{
  public:
    enum { MAXSTEP = 127 };
    struct strFxConsts
    {
        enum { FX_TYPE_NONE = 0, FX_TYPE_GLIDE = 20, FX_TYPE_FILTER_LOWPASS = 28, FX_TYPE_FILTER_BANDPASS = 30,
               FX_TYPE_R31 = 50, FX_TYPE_FILTER_ENV = FX_TYPE_R31 };
    };
    int16_t getFxMax(uint8_t, bool = false) { return 127; }
    int16_t getFxMin(uint8_t, bool = false) { return 0; }
    struct strPattern
    {
        struct strTrack
        {
            uint8_t length;
            struct strStep
            {
                int8_t note;
                uint8_t instrument;
                struct strFx { uint8_t type; uint8_t value; } fx[2];
                int8_t volume;
                int8_t micromove;
            } step[MAXSTEP + 1];
        } track[kAllTracks];
    };
    strPattern* getActualPattern() { return pattern; }
    bool isPlay() { return playing; }
    strPattern* pattern = nullptr;
    bool playing = false;
};
extern Sequencer sequencer;
