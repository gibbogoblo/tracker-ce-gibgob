/* SPDX-License-Identifier: GPL-3.0-only
 * GibGob OS PERC: the LXR-02's four drum voices (Erica Synths, GPL-3.0-only: Soundengine/DSPAudio
 * DrumVoice.c, Snare.c, CymbalVoice.c, HiHat.c, lfo.c), changed so that each voice is its own instance
 * (a pointer instead of the LXR's fixed voiceArray / snareVoice / cymbalVoice / hatVoice), so any GibGob
 * drum track can be any of them. The DSP is the LXR's own; what changed:
 *  - no front-panel faders, trigger outputs, modulation nodes or FX bus (the PERC engine does those)
 *  - the LFO is the LXR's waveforms and tempo sync, without its routing (percLfo_*)
 * Copyright (C) 2012-2026 Erica Synths; changes (C) 2026 GibGob OS. */
#ifndef LXR_PERC_VOICES_H_
#define LXR_PERC_VOICES_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "lxr/Soundengine/DSPAudio/1PoleLp.h"
#include "lxr/Soundengine/DSPAudio/Decay.h"
#include "lxr/Soundengine/DSPAudio/Oscillator.h"
#include "lxr/Soundengine/DSPAudio/ResonantFilter.h"
#include "lxr/Soundengine/DSPAudio/SlopeEg2.h"
#include "lxr/Soundengine/DSPAudio/distortion.h"
#include "lxr/Soundengine/DSPAudio/snapEg.h"
#include "lxr/Soundengine/DSPAudio/transientGenerator.h"
#include "lxr/Soundengine/config.h"

#define PERC_BLOCK OUTPUT_DMA_SIZE /* 64 samples: envelopes and LFOs run once a block, as on the LXR */

/* ---- the LXR's DrumVoice (Drum 1-3): FM oscillators, pitch EG, transient, filter ---- */
typedef struct
{
    OscInfo osc;
    OscInfo modOsc;
    float fmModAmount;
    float vol;
    float velo;
    uint8_t pan;
    float triggeredPanL;
    float triggeredPanR;
    DecayEg oscPitchEg;
    float egPitchModAmount;
    TransientGenerator transGen;
    SlopeEg2 oscVolEg;
    Distortion distortion;
    ResonantFilter filter;
    uint8_t filterType;
    double ampFilterInput;
    double lastGain;
    uint8_t mixOscs;
    SnapEg snapEg;
    uint8_t volumeMod;
    float pwm;
} PercDrum;

/* ---- the LXR's Snare: oscillator + noise, pitch EG, filter ---- */
typedef struct
{
    OscInfo osc;
    OscInfo noiseOsc;
    uint8_t filterType;
    float vol;
    uint8_t pan;
    float triggeredPanL;
    float triggeredPanR;
    float mix;
    float velo;
    ResonantFilter filter;
    DecayEg oscPitchEg;
    float egPitchModAmount;
    TransientGenerator transGen;
    SlopeEg2 oscVolEg;
    float egValueOscVol;
    Distortion distortion;
    SnapEg snapEg;
    uint8_t volumeMod;
    float pwm;
} PercSnare;

/* ---- the LXR's Cymbal and HiHat: 3-operator FM, filter (the hat with open / closed decays) ---- */
typedef struct
{
    OscInfo osc;
    OscInfo modOsc;
    OscInfo modOsc2;
    float fmModAmount1;
    float fmModAmount2;
    float vol;
    uint8_t pan;
    float triggeredPanL;
    float triggeredPanR;
    float velo;
    ResonantFilter filter;
    uint8_t filterType;
    TransientGenerator transGen;
    float decayClosed; /* the hat */
    float decayOpen;   /* the hat */
    Distortion distortion;
    SlopeEg2 oscVolEg;
    float egValueOscVol;
    SnapEg snapEg;
    uint8_t volumeMod;
    uint8_t isOpen;
    float pwm;
} PercMetal;

void PercDrum_init(PercDrum* v);
void PercDrum_trigger(PercDrum* v, uint8_t vel, uint8_t note);
void PercDrum_calcAsync(PercDrum* v);
void PercDrum_calcSyncBlock(PercDrum* v, int16_t* buf, uint8_t size);

void PercSnare_init(PercSnare* v);
void PercSnare_trigger(PercSnare* v, uint8_t vel, uint8_t note);
void PercSnare_calcAsync(PercSnare* v);
void PercSnare_calcSyncBlock(PercSnare* v, int16_t* buf, uint8_t size);

void PercCymbal_init(PercMetal* v);
void PercCymbal_trigger(PercMetal* v, uint8_t vel, uint8_t note);
void PercCymbal_calcAsync(PercMetal* v);
void PercCymbal_calcSyncBlock(PercMetal* v, int16_t* buf, uint8_t size);

void PercHat_init(PercMetal* v);
void PercHat_trigger(PercMetal* v, uint8_t vel, uint8_t isOpen, uint8_t note);
void PercHat_calcAsync(PercMetal* v);
void PercHat_calcSyncBlock(PercMetal* v, int16_t* buf, uint8_t size);

/* ---- the LXR's LFO (lfo.c): its waveforms, rate curve and tempo sync ---- */
enum
{
    PERC_LFO_SINE,
    PERC_LFO_TRI,
    PERC_LFO_SAW_UP,
    PERC_LFO_SAW_DOWN,
    PERC_LFO_REC,
    PERC_LFO_NOISE,
    PERC_LFO_EXP_UP,
    PERC_LFO_EXP_DOWN,
    PERC_LFO_WAVEFORMS
};
#define PERC_LFO_SYNCS 12 /* off, 4/1, 2/1, 1/1, 1/2, 1/3, 1/4, 1/6, 1/8, 1/12, 1/16, 1/32 */
typedef struct
{
    uint32_t phase;
    uint32_t phaseInc;
    uint32_t phaseOffset;
    uint8_t waveform;
    uint8_t sync;
    float freq;
    float rnd;
} PercLfo;
void percLfo_init(PercLfo* lfo);
float percLfo_calc(PercLfo* lfo); /* 0..1, once a block */
void percLfo_setFreq(PercLfo* lfo, float f01); /* the LXR's rate curve, 0..1 */
void percLfo_setSync(PercLfo* lfo, uint8_t sync);
void percLfo_recalc(PercLfo* lfo); /* after a tempo change */

#ifdef __cplusplus
}
#endif

#endif
