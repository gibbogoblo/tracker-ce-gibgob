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
    enum { STEP_NOTE_EMPTY = -1, STEP_VELO_DEFAULT = -1 };
    struct strFxConsts
    {
        enum { FX_TYPE_NONE = 0, FX_TYPE_GLIDE = 20, FX_TYPE_FILTER_LOWPASS = 28, FX_TYPE_FILTER_HIGHPASS = 29,
               FX_TYPE_FILTER_BANDPASS = 30, FX_TYPE_DELAY_SEND = 31, FX_TYPE_PANNING = 32, FX_TYPE_REVERB_SEND = 33,
               FX_TYPE_REVERSE_PLAYBACK = 27, FX_TYPE_POSITION = 23, FX_TYPE_VOLUME_LFO = 24, FX_TYPE_FINETUNE_LFO = 34,
               FX_TYPE_FILTER_LFO = 36, FX_TYPE_OVERDRIVE = 39, FX_TYPE_BIT_DEPTH = 40, FX_TYPE_TUNE = 41,
               FX_TYPE_SLIDE_UP = 42, FX_TYPE_SLIDE_DOWN = 43,
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
        float swingRez = 0.0f;
    };
    strPattern* getActualPattern() { return pattern; }
    bool isPlay() { return playing; }
    float getCurrentTempo() { return tempo; }
    float tempo = 120.0f;
    void startCountIn() {}
    bool isCountingIn() { return false; }
    void playPattern() { playing = true; }
    strPattern* pattern = nullptr;
    bool playing = false;
};
extern Sequencer sequencer;
