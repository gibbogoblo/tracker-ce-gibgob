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
