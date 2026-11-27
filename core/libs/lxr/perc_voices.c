/* SPDX-License-Identifier: GPL-3.0-only
 * GibGob OS PERC: the LXR-02's drum voices as instances (see perc_voices.h).
 * After Erica Synths' LXR-02 firmware (lxr-desktop-app, Core/Src/Soundengine/DSPAudio: DrumVoice.c,
 * Snare.c, CymbalVoice.c, HiHat.c, lfo.c), Copyright (C) 2012-2026 Erica Synths, GPL-3.0-only, itself
 * built on Julian Schmidt's Sonic Potions LXR. Changes (C) 2026 GibGob OS: voice pointers instead of
 * globals; no faders, trigger outs, modulation nodes or FX bus. */
#include "lxr/perc_voices.h"

#include "lxr/Soundengine/DSPAudio/BufferTools.h"
#include "lxr/Soundengine/DSPAudio/wavetable.h"
#include "lxr/Soundengine/Sequencer/sequencer.h"

#include <math.h>

/* ======================================================= drum (DrumVoice.c) */

static void drum_updatePan(PercDrum* v)
{
    v->triggeredPanL = sqrtf((127 - v->pan) / 127.f);
    v->triggeredPanR = sqrtf(v->pan / 127.f);
}

void PercDrum_init(PercDrum* v)
{
    memset(v, 0, sizeof(*v));
    initOnePole(&v->distortion.DriveParamFilter);
    setOnePoleCoef(&v->distortion.DriveParamFilter, 0.01);
    SnapEg_init(&v->snapEg);
    v->pan = 64;
    v->vol = 0.8f;
    v->fmModAmount = 0.5f;
    transient_init(&v->transGen);
    SVF_init(&v->filter);
    v->filterType = 0x01;

    v->modOsc.freq = 440;
    v->modOsc.waveform = 1;
    v->modOsc.fmMod = 0;
    v->modOsc.midiFreq = 70 << 8 | 64;
    v->modOsc.pitchMod = 1.0f;
    v->modOsc.modNodeValue = 1;
    v->modOsc.baseNote = SEQ_DEFAULT_NOTE;

    v->volumeMod = 1;

    v->osc.freq = 440;
    v->osc.modNodeValue = 1;
    v->osc.waveform = TRI;
    v->osc.fmMod = 0;
    v->osc.midiFreq = 70 << 8 | 64;
    v->osc.pitchMod = 1.0f;
    v->osc.baseNote = SEQ_DEFAULT_NOTE;

    DecayEg_init(&v->oscPitchEg);
    v->egPitchModAmount = 0.5f;
    slopeEg2_init(&v->oscVolEg);
    setDistortionShape(&v->distortion, 0.f);
    v->mixOscs = 1;
    drum_updatePan(v);
}

void PercDrum_trigger(PercDrum* v, uint8_t vol, uint8_t note)
{
    float offset = 1;
    if (v->transGen.waveform == 1) /* offset mode */
        offset -= v->transGen.volume;
    if (v->osc.waveform == SINE)
        v->osc.phase = 1024 + ((0x3ff << 20) - 1024) * offset;
    else if (v->osc.waveform > SINE && v->osc.waveform <= REC)
        v->osc.phase = (0xff << 22) * offset;
    else
        v->osc.phase = 0;

    /* reset mod osc phase */
    if (v->modOsc.waveform == SINE)
        v->modOsc.phase = 1024 + ((0x3ff << 20) - 1024);
    else if (v->modOsc.waveform > SINE && v->modOsc.waveform <= REC)
        v->modOsc.phase = (0xff << 22);
    else
        v->modOsc.phase = 0;

    osc_setBaseNote(&v->osc, note);
    osc_setBaseNote(&v->modOsc, note);
    DecayEg_trigger(&v->oscPitchEg);
    slopeEg2_trigger(&v->oscVolEg);
    v->velo = vol / 127.f;
    transient_trigger(&v->transGen);
    SnapEg_trigger(&v->snapEg);
    SVF_reset(&v->filter); /* reset filter coeffs to prevent wrong transient */
    drum_updatePan(v);
}

IN_ITCM_RAM void PercDrum_calcAsync(PercDrum* v)
{
    const float egPitchVal = DecayEg_calc(&v->oscPitchEg);
    const float pitchEgValue = egPitchVal * v->egPitchModAmount;
    v->osc.pitchMod = 1 + pitchEgValue;

    if (v->transGen.waveform == 0) /* snap EG with transient 0 */
    {
        const float snapVal = SnapEg_calc(&v->snapEg, v->transGen.pitch);
        v->osc.pitchMod += snapVal * v->transGen.volume;
    }
    v->osc.fmMod = v->fmModAmount * egPitchVal;

    /* the amp EG once a block (no interpolation in its attack when attack = 0) */
    if ((v->oscVolEg.attack == 1) && ((v->oscVolEg.state == EG_A) || (v->oscVolEg.state == EG_REPEAT)))
    {
        v->ampFilterInput = slopeEg2_calc(&v->oscVolEg);
        v->lastGain = v->ampFilterInput;
    }
    else
    {
        v->lastGain = v->ampFilterInput;
        v->ampFilterInput = slopeEg2_calc(&v->oscVolEg);
    }
    osc_setFreq(&v->osc);
    osc_setFreq(&v->modOsc);
}

IN_ITCM_RAM void PercDrum_calcSyncBlock(PercDrum* v, int16_t* buf, uint8_t size)
{
    int16_t modBuf[PERC_BLOCK];
    calcNextOscSampleBlock(&v->modOsc, modBuf, size, v->fmModAmount, v->pwm);
    if (v->mixOscs)
    {
        calcNextOscSampleBlock(&v->osc, buf, size, (1.f - v->fmModAmount), v->pwm);
        bufferTool_addBuffersSaturating(buf, modBuf, size);
    }
    else
    {
        calcNextOscSampleFmBlock(&v->osc, modBuf, buf, size, 1.0f, v->pwm);
    }
    transient_calcBlock(&v->transGen, modBuf, size);
    bufferTool_addBuffersSaturating(buf, modBuf, size);
    SVF_calcBlockZDF(&v->filter, v->filterType, buf, size);
    bufferTool_addGainInterpolated(buf, v->ampFilterInput, v->lastGain, size);
    if (v->volumeMod)
        bufferTool_addGain(buf, v->velo, size);
    calcDistBlockFiltered(&v->distortion, buf, size);
    for (int i = 0; i < size; i++)
        buf[i] *= v->vol;
}

/* ======================================================= snare (Snare.c) */

static void snare_updatePan(PercSnare* v)
{
    v->triggeredPanL = sqrtf((127 - v->pan) / 127.f);
    v->triggeredPanR = sqrtf(v->pan / 127.f);
}

void PercSnare_init(PercSnare* v)
{
    memset(v, 0, sizeof(*v));
    SnapEg_init(&v->snapEg);
    v->pan = 64;
    v->vol = 0.8f;

    v->noiseOsc.freq = 440;
    v->noiseOsc.waveform = 1;
    v->noiseOsc.fmMod = 0;
    v->noiseOsc.midiFreq = 70 << 8 | 64;
    v->noiseOsc.pitchMod = 1.0f;
    v->noiseOsc.modNodeValue = 1;
    v->noiseOsc.baseNote = SEQ_DEFAULT_NOTE;

    v->osc.freq = 440;
    v->osc.waveform = 1;
    v->osc.fmMod = 0;
    v->osc.midiFreq = 70 << 8 | 64;
    v->osc.modNodeValue = 1;
    v->osc.pitchMod = 1.0f;
    v->osc.baseNote = SEQ_DEFAULT_NOTE;

    setDistortionShape(&v->distortion, 0.f);
    v->volumeMod = 1;
    transient_init(&v->transGen);
    DecayEg_init(&v->oscPitchEg);
    v->egPitchModAmount = 0.5f;
    slopeEg2_init(&v->oscVolEg);
    SVF_init(&v->filter);
    v->filterType = 0x01;
    snare_updatePan(v);
}

void PercSnare_trigger(PercSnare* v, uint8_t vel, uint8_t note)
{
    float offset = 1;
    if (v->transGen.waveform == 1)
        offset -= v->transGen.volume;
    if (v->osc.waveform == SINE)
        v->osc.phase = (0x3ff << 20) * offset;
    else if (v->osc.waveform > SINE && v->osc.waveform <= REC)
        v->osc.phase = (0xff << 22) * offset;
    else
        v->osc.phase = 0;
    DecayEg_trigger(&v->oscPitchEg);
    slopeEg2_trigger(&v->oscVolEg);
    v->velo = vel / 127.f;
    osc_setBaseNote(&v->osc, note);
    transient_trigger(&v->transGen);
    SnapEg_trigger(&v->snapEg);
    snare_updatePan(v);
}

IN_ITCM_RAM void PercSnare_calcAsync(PercSnare* v)
{
    const float egPitchVal = DecayEg_calc(&v->oscPitchEg);
    v->osc.pitchMod = 1 + egPitchVal * v->egPitchModAmount;
    v->egValueOscVol = slopeEg2_calc(&v->oscVolEg);
    if (v->transGen.waveform == 0)
    {
        const float snapVal = SnapEg_calc(&v->snapEg, v->transGen.pitch);
        v->osc.pitchMod += snapVal * v->transGen.volume;
    }
    osc_setFreq(&v->osc);
    osc_setFreq(&v->noiseOsc);
}

IN_ITCM_RAM void PercSnare_calcSyncBlock(PercSnare* v, int16_t* buf, uint8_t size)
{
    int16_t transBuf[PERC_BLOCK];
    int16_t oscBuf[PERC_BLOCK];
    calcNoiseBlock(&v->noiseOsc, buf, size, 0.9f);
    SVF_calcBlockZDF(&v->filter, v->filterType, buf, size);
    calcNextOscSampleBlock(&v->osc, oscBuf, size, (1.f - v->mix), v->pwm);
    transient_calcBlock(&v->transGen, transBuf, size);
    const float gain = v->volumeMod ? v->vol * v->velo * v->egValueOscVol : v->vol * v->egValueOscVol;
    for (int j = 0; j < size; j++)
    {
        float tmp = fmaf(buf[j], v->mix, oscBuf[j]);
        tmp += transBuf[j];
        buf[j] = gain * tmp;
    }
    calcDistBlock(&v->distortion, buf, size);
}

/* ======================================================= cymbal and hat (CymbalVoice.c, HiHat.c) */

static void metal_updatePan(PercMetal* v)
{
    v->triggeredPanL = sqrtf((127 - v->pan) / 127.f);
    v->triggeredPanR = sqrtf(v->pan / 127.f);
}

static void metal_init(PercMetal* v)
{
    memset(v, 0, sizeof(*v));
    SnapEg_init(&v->snapEg);
    v->pan = 64;
    v->vol = 0.8f;
    transient_init(&v->transGen);
    v->fmModAmount1 = 0.5f;
    v->fmModAmount2 = 0.5f;
    setDistortionShape(&v->distortion, 0.f);

    v->modOsc.freq = 440;
    v->modOsc.waveform = SINE;
    v->modOsc.fmMod = 0;
    v->modOsc.midiFreq = 70 << 8 | 64;
    v->modOsc.pitchMod = 1.0f;
    v->modOsc.modNodeValue = 1;
    v->modOsc.baseNote = SEQ_DEFAULT_NOTE;

    v->modOsc2.freq = 440;
    v->modOsc2.waveform = NOISE;
    v->modOsc2.fmMod = 0;
    v->modOsc2.midiFreq = 70 << 8 | 64;
    v->modOsc2.pitchMod = 1.0f;
    v->modOsc2.modNodeValue = 1;
    v->modOsc2.baseNote = SEQ_DEFAULT_NOTE;

    v->osc.freq = 440;
    v->osc.waveform = 1;
    v->osc.fmMod = 1;
    v->osc.midiFreq = 70 << 8 | 64;
    v->osc.pitchMod = 1.0f;
    v->osc.modNodeValue = 1;
    v->osc.baseNote = SEQ_DEFAULT_NOTE;

    v->volumeMod = 1;
    slopeEg2_init(&v->oscVolEg);
    SVF_init(&v->filter);
    v->filterType = 0x01;
    metal_updatePan(v);
}

static void metal_trigger(PercMetal* v, uint8_t vel, uint8_t note)
{
    float offset = 1;
    if (v->transGen.waveform == 1)
        offset -= v->transGen.volume;
    if (v->osc.waveform == SINE)
        v->osc.phase = (0x3ff << 20) * offset;
    else if (v->osc.waveform > SINE && v->osc.waveform <= REC)
        v->osc.phase = (0xff << 22) * offset;
    else
        v->osc.phase = 0;
    v->modOsc.phase = 0;
    v->modOsc2.phase = 0;
    osc_setBaseNote(&v->osc, note);
    osc_setBaseNote(&v->modOsc, note);
    osc_setBaseNote(&v->modOsc2, note);
    v->velo = vel / 127.f;
}

IN_ITCM_RAM static void metal_calcAsync(PercMetal* v)
{
    v->egValueOscVol = slopeEg2_calc(&v->oscVolEg);
    if (v->transGen.waveform == 0)
    {
        const float snapVal = SnapEg_calc(&v->snapEg, v->transGen.pitch);
        v->osc.pitchMod = 1 + snapVal * v->transGen.volume;
    }
    osc_setFreq(&v->osc);
    osc_setFreq(&v->modOsc);
    osc_setFreq(&v->modOsc2);
}

IN_ITCM_RAM static void metal_mixOut(PercMetal* v, int16_t* buf, int16_t* mod, uint8_t size)
{
    const float gain = v->volumeMod ? v->vol * v->velo * v->egValueOscVol : v->vol * v->egValueOscVol;
    for (int i = 0; i < size; i++)
    {
        const int32_t s = (int32_t)buf[i] + (int32_t)mod[i]; /* __QADD16: a saturating add */
        buf[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
        buf[i] *= gain;
    }
    calcDistBlock(&v->distortion, buf, size);
}

void PercCymbal_init(PercMetal* v)
{
    metal_init(v);
}

void PercCymbal_trigger(PercMetal* v, uint8_t vel, uint8_t note)
{
    metal_trigger(v, vel, note);
    slopeEg2_trigger(&v->oscVolEg);
    transient_trigger(&v->transGen);
    SnapEg_trigger(&v->snapEg);
    metal_updatePan(v);
}

IN_ITCM_RAM void PercCymbal_calcAsync(PercMetal* v)
{
    metal_calcAsync(v);
}

IN_ITCM_RAM void PercCymbal_calcSyncBlock(PercMetal* v, int16_t* buf, uint8_t size)
{
    int16_t mod[PERC_BLOCK], mod2[PERC_BLOCK];
    calcNextOscSampleBlock(&v->modOsc, mod, size, v->fmModAmount1, v->pwm);
    calcNextOscSampleBlock(&v->modOsc2, mod2, size, v->fmModAmount2, v->pwm);
    bufferTool_addBuffersSaturating(mod, mod2, size);
    calcNextOscSampleFmBlock(&v->osc, mod, buf, size, 1.f, v->pwm);
    SVF_calcBlockZDF(&v->filter, v->filterType, buf, size);
    transient_calcBlock(&v->transGen, mod, size);
    metal_mixOut(v, buf, mod, size);
}

void PercHat_init(PercMetal* v)
{
    metal_init(v);
}

void PercHat_trigger(PercMetal* v, uint8_t vel, uint8_t isOpen, uint8_t note)
{
    metal_trigger(v, vel, note);
    v->isOpen = isOpen;
    v->oscVolEg.decay = isOpen ? v->decayOpen : v->decayClosed;
    slopeEg2_trigger(&v->oscVolEg);
    transient_trigger(&v->transGen);
    SnapEg_trigger(&v->snapEg);
    metal_updatePan(v);
}

IN_ITCM_RAM void PercHat_calcAsync(PercMetal* v)
{
    metal_calcAsync(v);
}

IN_ITCM_RAM void PercHat_calcSyncBlock(PercMetal* v, int16_t* buf, uint8_t size)
{
    int16_t mod1[PERC_BLOCK], mod2[PERC_BLOCK];
    calcNextOscSampleBlock(&v->modOsc, mod1, size, v->fmModAmount1, v->pwm);
    calcNextOscSampleBlock(&v->modOsc2, mod2, size, v->fmModAmount2, v->pwm);
    bufferTool_addBuffersSaturating(mod1, mod2, size);
    calcNextOscSampleFmBlock(&v->osc, mod1, buf, size, 0.5f, v->pwm);
    SVF_calcBlockZDF(&v->filter, v->filterType, buf, size);
    transient_calcBlock(&v->transGen, mod1, size);
    metal_mixOut(v, buf, mod1, size);
}

/* ======================================================= LFO (lfo.c) */

#define PERC_LFO_SR (REAL_FS / (float)OUTPUT_DMA_SIZE)
#define PERC_LFO_MAX_F 200.f

void percLfo_init(PercLfo* lfo)
{
    memset(lfo, 0, sizeof(*lfo));
    lfo->phaseInc = 1;
    lfo->freq = 1;
}

IN_ITCM_RAM float percLfo_calc(PercLfo* lfo)
{
    const uint32_t oldPhase = lfo->phase;
    lfo->phase += lfo->phaseInc;
    const uint8_t overflow = oldPhase > lfo->phase;
    switch (lfo->waveform)
    {
        case PERC_LFO_SINE: return ((sine_table[(lfo->phase >> 20)] / 32767.f) + 1) / 2.f;
        case PERC_LFO_TRI: return (1.f - fabsf((lfo->phase / (float)0xffffffff) * 2 - 1));
        case PERC_LFO_SAW_UP: return lfo->phase / (float)0xffffffff;
        case PERC_LFO_REC: return lfo->phase > 0x7fffffff ? 1.f : 0.f;
        case PERC_LFO_NOISE:
            if (overflow)
                lfo->rnd = GetRngValue() / (float)0xffffffff;
            return lfo->rnd;
        case PERC_LFO_SAW_DOWN: return (0xffffffff - (lfo->phase)) / (float)0xffffffff;
        case PERC_LFO_EXP_UP:
        {
            const float x = lfo->phase / (float)0xffffffff;
            return x * x * x;
        }
        case PERC_LFO_EXP_DOWN:
        {
            const float x = (0xffffffff - (lfo->phase)) / (float)0xffffffff;
            return x * x * x;
        }
        default: return 0;
    }
}

static uint32_t percLfo_phaseInc(float freq, uint8_t sync)
{
    static const float kScaler[PERC_LFO_SYNCS] = { 0, 0.25f, 0.5f, 1, 2, 3, 4, 6, 8, 12, 16, 32 };
    if (sync == 0 || sync >= PERC_LFO_SYNCS)
        return (uint32_t)(freq / (PERC_LFO_SR) * 0xffffffff);
    const float bpm = seq_getBpm();
    const float bar = bpm > 0.0f ? bpm / 60.f / 4 : 0.0f;
    return (uint32_t)((bar * kScaler[sync] / (PERC_LFO_SR)) * 0xffffffff);
}

void percLfo_setFreq(PercLfo* lfo, float f)
{
    f = 0.0078125f + f * (1.f - 0.0078125f);
    f = f * f * f;
    lfo->freq = f * PERC_LFO_MAX_F;
    lfo->phaseInc = percLfo_phaseInc(lfo->freq, lfo->sync);
}

void percLfo_setSync(PercLfo* lfo, uint8_t sync)
{
    lfo->sync = sync;
    lfo->phaseInc = percLfo_phaseInc(lfo->freq, lfo->sync);
}

void percLfo_recalc(PercLfo* lfo)
{
    lfo->phaseInc = percLfo_phaseInc(lfo->freq, lfo->sync);
}
