// SPDX-License-Identifier: GPL-3.0-only
// GibGob OS PERC (see Perc.h). The values are set the way the LXR-02's MidiParser.c sets its sound
// parameters (Erica Synths, GPL-3.0-only), one voice per GibGob track instead of one per LXR slot.
#include "Perc.h"

#include "PercKits.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

extern "C" volatile uint16_t lxr_bpm;

namespace gibgob
{
namespace perc
{

using namespace lxr;

const char* const kVoiceTypeNames[kVoiceTypeCount] = { "Drum", "Snare", "Cymbal", "Hihat" };
const char* const kKitSlotNames[kKitSlotCount] = { "D1", "D2", "D3", "SN", "CY", "CH", "OH" };

namespace
{
constexpr uint8_t D = 1 << kDrum, S = 1 << kSnare, C = 1 << kCymbal, H = 1 << kHihat;
constexpr uint8_t ALL = D | S | C | H, CH = C | H;
constexpr uint8_t kDestMax = kParamCount; // 0 = none, else Param + 1
constexpr uint8_t kWaveMax = 5;           // sine .. pwm (the LXR's user samples are not here)

const ParamInfo kInfo[kParamCount] = {
    { "Wave", kWaveMax, ALL },    { "Tune", 127, ALL },         { "Fine", 127, ALL },
    { "PWM", 127, ALL },          { "Attack", 127, ALL },       { "Decay", 127, ALL },
    { "Open", 127, H },           { "Slope", 127, ALL },        { "Repeat", 127, S | C },
    { "P Decay", 127, D | S },    { "P Slope", 127, D | S },    { "P Amount", 127, D | S },
    { "FM Amt", 127, D },         { "FM Tune", 127, D },        { "FM Wave", kWaveMax, D },
    { "Mix", 1, D },              { "N Freq", 127, S },         { "N Mix", 127, S },
    { "Wave 2", kWaveMax, CH },   { "Wave 3", kWaveMax, CH },   { "Tune 2", 127, CH },
    { "Tune 3", 127, CH },        { "Depth 2", 127, CH },       { "Depth 3", 127, CH },
    { "Click", 13, ALL },         { "Clk Vol", 127, ALL },      { "Clk Tune", 127, ALL },
    { "Cutoff", 127, ALL },       { "Reso", 127, ALL },         { "Filter", 7, ALL },
    { "F Drive", 127, ALL },      { "Volume", 127, ALL },       { "Pan", 127, ALL },
    { "Rate", 127, ALL },         { "Drive", 127, ALL },        { "Vel Amt", 127, ALL },
    { "Vel Dest", kDestMax, ALL }, { "Vel Vol", 1, ALL },       { "Speed", 127, ALL },
    { "Sync", PERC_LFO_SYNCS - 1, ALL }, { "Amount", 127, ALL }, { "Shape", PERC_LFO_WAVEFORMS - 1, ALL },
    { "Retrig", 1, ALL },         { "Phase", 127, ALL },        { "Dest", kDestMax, ALL },
    { "Speed", 127, ALL },        { "Sync", PERC_LFO_SYNCS - 1, ALL }, { "Amount", 127, ALL },
    { "Shape", PERC_LFO_WAVEFORMS - 1, ALL }, { "Retrig", 1, ALL }, { "Phase", 127, ALL },
    { "Dest", kDestMax, ALL },
};

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// the LXR's valueShaperF2F
float shaper(float val, float shape)
{
    const float k = 2 * shape / (1.0001f - shape);
    return (1 + k) * val / (1 + k * fabsf(val));
}

void setCoarse(OscInfo* o, uint8_t v)
{
    o->midiFreq = (uint16_t)((o->midiFreq & 0x00ff) | (v << 8));
    osc_recalcFreq(o);
}
void setFine(OscInfo* o, uint8_t v)
{
    o->midiFreq = (uint16_t)((o->midiFreq & 0xff00) | v);
    osc_recalcFreq(o);
}

// the parts every voice kind has, wherever they are in its struct
struct Parts
{
    OscInfo* osc;
    ResonantFilter* filter;
    uint8_t* filterType;
    SlopeEg2* eg;
    TransientGenerator* trans;
    Distortion* dist;
    float* vol;
    uint8_t* pan;
    uint8_t* volumeMod;
    float* pwm;
};
Parts partsOf(VoiceState& s)
{
    Parts p;
    switch (s.sound.type)
    {
        case kDrum:
        {
            PercDrum& v = s.dsp.drum;
            p = { &v.osc, &v.filter, &v.filterType, &v.oscVolEg, &v.transGen, &v.distortion, &v.vol, &v.pan, &v.volumeMod, &v.pwm };
            break;
        }
        case kSnare:
        {
            PercSnare& v = s.dsp.snare;
            p = { &v.osc, &v.filter, &v.filterType, &v.oscVolEg, &v.transGen, &v.distortion, &v.vol, &v.pan, &v.volumeMod, &v.pwm };
            break;
        }
        default:
        {
            PercMetal& v = s.dsp.metal;
            p = { &v.osc, &v.filter, &v.filterType, &v.oscVolEg, &v.transGen, &v.distortion, &v.vol, &v.pan, &v.volumeMod, &v.pwm };
            break;
        }
    }
    return p;
}

// one value into the DSP, as the LXR's MidiParser does (itg 0..max, f = itg / 127)
IN_ITCM_RAM void apply(VoiceState& s, int param, int itg)
{
    itg = clampi(itg, 0, kInfo[param].max);
    const float f = itg / 127.f;
    const int type = s.sound.type;
    Parts p = partsOf(s);
    switch (param)
    {
        case pWave: p.osc->waveform = (uint8_t)itg; break;
        case pCoarse: setCoarse(p.osc, (uint8_t)itg); break;
        case pFine: setFine(p.osc, (uint8_t)itg); break;
        case pPwm: *p.pwm = f; break;
        case pAttack: slopeEg2_setAttack(p.eg, (uint8_t)itg, 0); break;
        case pDecay:
            if (type == kHihat)
            {
                s.dsp.metal.decayClosed = (float)slopeEg2_calcDecay((uint8_t)itg);
                if (!s.dsp.metal.isOpen)
                    p.eg->decay = s.dsp.metal.decayClosed;
            }
            else
                slopeEg2_setDecay(p.eg, (uint8_t)itg, 0);
            break;
        case pDecayOpen:
            if (type == kHihat)
            {
                s.dsp.metal.decayOpen = (float)slopeEg2_calcDecay((uint8_t)itg);
                if (s.dsp.metal.isOpen)
                    p.eg->decay = s.dsp.metal.decayOpen;
            }
            break;
        case pSlope: slopeEg2_setSlope(p.eg, (uint8_t)itg); break;
        case pRepeat:
            if (type == kSnare || type == kCymbal)
                p.eg->repeat = (uint8_t)itg;
            break;
        case pPitchDecay:
            if (type == kDrum)
                DecayEg_setDecay(&s.dsp.drum.oscPitchEg, (uint8_t)itg);
            else if (type == kSnare)
                DecayEg_setDecay(&s.dsp.snare.oscPitchEg, (uint8_t)itg);
            break;
        case pPitchSlope:
            if (type == kDrum)
                DecayEg_setSlope(&s.dsp.drum.oscPitchEg, (uint8_t)(itg > 126 ? 126 : itg));
            else if (type == kSnare)
                DecayEg_setSlope(&s.dsp.snare.oscPitchEg, (uint8_t)(itg > 126 ? 126 : itg));
            break;
        case pPitchAmount:
            if (type == kDrum)
                s.dsp.drum.egPitchModAmount = f * f * PITCH_AMOUNT_FACTOR;
            else if (type == kSnare)
                s.dsp.snare.egPitchModAmount = f * f * PITCH_AMOUNT_FACTOR;
            break;
        case pFmAmount:
            if (type == kDrum)
                s.dsp.drum.fmModAmount = f;
            break;
        case pFmFreq:
            if (type == kDrum)
                setCoarse(&s.dsp.drum.modOsc, (uint8_t)itg);
            break;
        case pModWave:
            if (type == kDrum)
                s.dsp.drum.modOsc.waveform = (uint8_t)itg;
            break;
        case pMixMode:
            if (type == kDrum)
                s.dsp.drum.mixOscs = itg != 0;
            break;
        case pNoiseFreq:
            if (type == kSnare)
                s.dsp.snare.noiseOsc.freq = f * 22000;
            break;
        case pNoiseMix:
            if (type == kSnare)
                s.dsp.snare.mix = f;
            break;
        case pWave2:
            if (type >= kCymbal)
                s.dsp.metal.modOsc.waveform = (uint8_t)itg;
            break;
        case pWave3:
            if (type >= kCymbal)
                s.dsp.metal.modOsc2.waveform = (uint8_t)itg;
            break;
        case pModF1:
            if (type >= kCymbal)
                setCoarse(&s.dsp.metal.modOsc, (uint8_t)itg);
            break;
        case pModF2:
            if (type >= kCymbal)
                setCoarse(&s.dsp.metal.modOsc2, (uint8_t)itg);
            break;
        case pModGain1:
            if (type >= kCymbal)
                s.dsp.metal.fmModAmount1 = f;
            break;
        case pModGain2:
            if (type >= kCymbal)
                s.dsp.metal.fmModAmount2 = f;
            break;
        case pTransWave: transient_setWaveform(p.trans, (uint8_t)itg); break;
        case pTransVol: p.trans->volume = f; break;
        case pTransFreq: p.trans->pitch = 1.f + ((itg / 33.9f) - 0.75f); break;
        case pFilterFreq: SVF_directSetFilterValue(p.filter, shaper(f, FILTER_SHAPER)); break;
        case pFilterReso: SVF_setReso(p.filter, f); break;
        case pFilterType: *p.filterType = (uint8_t)(itg + 1); break;
        case pFilterDrive: SVF_setDrive(p.filter, f); break;
        case pVolume: *p.vol = f; break;
        case pPan: *p.pan = (uint8_t)itg; break;
        case pSampleRate:
            s.decimRate = shaper(f, -0.7f);
            if (s.decimRate < 1.f / 64)
                s.decimRate = 1.f / 64; // 0 would hold one sample for ever
            break;
        case pDrive: setDistortionShape(p.dist, f); break;
        case pVelVolume: *p.volumeMod = itg != 0; break;
        case pLfoFreq: percLfo_setFreq(&s.lfo[0], f); break;
        case pLfoSync: percLfo_setSync(&s.lfo[0], (uint8_t)itg); break;
        case pLfoWave: s.lfo[0].waveform = (uint8_t)itg; break;
        case pLfoOffset: s.lfo[0].phaseOffset = (uint32_t)(f * 4294967295.f); break;
        case pLfo2Freq: percLfo_setFreq(&s.lfo[1], f); break;
        case pLfo2Sync: percLfo_setSync(&s.lfo[1], (uint8_t)itg); break;
        case pLfo2Wave: s.lfo[1].waveform = (uint8_t)itg; break;
        case pLfo2Offset: s.lfo[1].phaseOffset = (uint32_t)(f * 4294967295.f); break;
        default: break; // amounts, destinations, retrigger: read when they are used
    }
}


// ---- the kits: which of the LXR's 247 values is a GibGob value, per LXR voice (0..5) ----
int lxrVoiceOfSlot(int slot) { return slot <= kSlotCymbal ? slot : 5; }

int parFor(int vi, int param)
{
    const bool drum = vi < 3;
    switch (param)
    {
        case pWave:
        {
            static const uint16_t w[6] = { PAR_OSC_WAVE_DRUM1, PAR_OSC_WAVE_DRUM2, PAR_OSC_WAVE_DRUM3, PAR_OSC_WAVE_SNARE, PAR_WAVE1_CYM, PAR_WAVE1_HH };
            return w[vi];
        }
        case pCoarse: return PAR_COARSE1 + 2 * vi;
        case pFine: return PAR_FINE1 + 2 * vi;
        case pPwm: return PAR_PWM_1 + vi;
        case pAttack: return PAR_VELOA1 + 2 * vi;
        case pDecay: return vi == 5 ? PAR_VELOD6_CLOSED : PAR_VELOD1 + 2 * vi;
        case pDecayOpen: return vi == 5 ? PAR_VELOD6_OPEN : 0;
        case pSlope: return PAR_VOL_SLOPE1 + vi;
        case pRepeat: return vi == 3 ? PAR_REPEAT4 : vi == 4 ? PAR_REPEAT5 : 0;
        case pPitchDecay: return vi <= 3 ? PAR_MOD_EG1 + vi : 0;
        case pPitchSlope: return vi <= 3 ? PAR_PITCH_SLOPE1 + vi : 0;
        case pPitchAmount: return vi <= 3 ? PAR_MODAMNT1 + vi : 0;
        case pFmAmount: return drum ? PAR_FMAMNT1 + 2 * vi : 0;
        case pFmFreq: return drum ? PAR_FM_FREQ1 + 2 * vi : 0;
        case pModWave: return drum ? PAR_MOD_WAVE_DRUM1 + vi : 0;
        case pMixMode: return drum ? PAR_MIX_MOD_1 + vi : 0;
        case pNoiseFreq: return vi == 3 ? PAR_NOISE_FREQ1 : 0;
        case pNoiseMix: return vi == 3 ? PAR_MIX1 : 0;
        case pWave2: return vi == 4 ? PAR_WAVE2_CYM : vi == 5 ? PAR_WAVE2_HH : 0;
        case pWave3: return vi == 4 ? PAR_WAVE3_CYM : vi == 5 ? PAR_WAVE3_HH : 0;
        case pModF1: return vi == 4 ? PAR_MOD_OSC_F1_CYM : vi == 5 ? PAR_MOD_OSC_F1 : 0;
        case pModF2: return vi == 4 ? PAR_MOD_OSC_F2_CYM : vi == 5 ? PAR_MOD_OSC_F2 : 0;
        case pModGain1: return vi == 4 ? PAR_MOD_OSC_GAIN1_CYM : vi == 5 ? PAR_MOD_OSC_GAIN1 : 0;
        case pModGain2: return vi == 4 ? PAR_MOD_OSC_GAIN2_CYM : vi == 5 ? PAR_MOD_OSC_GAIN2 : 0;
        case pTransWave: return PAR_TRANS1_WAVE + vi;
        case pTransVol: return PAR_TRANS1_VOL + vi;
        case pTransFreq: return PAR_TRANS1_FREQ + vi;
        case pFilterFreq: return PAR_FILTER_FREQ_1 + vi;
        case pFilterReso: return PAR_RESO_1 + vi;
        case pFilterType: return PAR_FILTER_TYPE_1 + vi;
        case pFilterDrive: return PAR_FILTER_DRIVE_1 + vi;
        case pVolume: return PAR_VOL1 + vi;
        case pPan: return drum ? PAR_PAN1 + vi : PAR_PAN4 + (vi - 3);
        case pSampleRate: return PAR_VOICE_DECIMATION1 + vi;
        case pDrive: return PAR_DRIVE1 + vi; // DRIVE1..3, SNARE_, CYMBAL_, HAT_DISTORTION
        case pVelAmount: return PAR_VELO_MOD_AMT_1 + vi;
        case pVelDest: return PAR_VEL_DEST_1 + vi;
        case pVelVolume: return PAR_VOLUME_MOD_ON_OFF1 + vi;
        default: return 0; // the LFOs: see lfoFromKit
    }
}

// a kit's modulation target (an index into the LXR's modTargets) as a GibGob destination of voice vi
uint8_t destFromKit(int vi, int target)
{
    if (target <= 0 || target >= kModTargetCount)
        return 0;
    const int par = kModTargetPar[target];
    if (par == 0)
        return 0;
    for (int p = 0; p < pVelAmount; p++)
        if (parFor(vi, p) == par)
            return (uint8_t)(p + 1);
    // the LFO's own speed and amount
    if (par == PAR_FREQ_LFO1 + vi)
        return pLfoFreq + 1;
    if (par == PAR_AMOUNT_LFO1 + vi)
        return pLfoAmount + 1;
    return 0;
}

void initVoiceDsp(VoiceState& s)
{
    switch (s.sound.type)
    {
        case kDrum: PercDrum_init(&s.dsp.drum); break;
        case kSnare: PercSnare_init(&s.dsp.snare); break;
        case kCymbal: PercCymbal_init(&s.dsp.metal); break;
        default: PercHat_init(&s.dsp.metal); break;
    }
    percLfo_init(&s.lfo[0]);
    percLfo_init(&s.lfo[1]);
    s.decimRate = 1;
    s.decimCount = 0;
    s.decimHold = 0;
    s.modded = 0;
    s.live = 0;
    s.level = 0;
    for (int p = 0; p < kParamCount; p++)
        apply(s, p, s.sound.value[p]);
    // the drum's drive smoothing starts where the sound has it (the LXR's has long settled)
    if (s.sound.type == kDrum)
        setOnePoleValue(&s.dsp.drum.distortion.DriveParamFilter, s.dsp.drum.distortion.shape);
}

const char* const kWaveNames[] = { "Sine", "Tri", "Saw", "Square", "Noise", "PWM" };
const char* const kTransNames[] = { "Snap", "Offset", "Click", "Click 2", "Tick", "Kick", "Rim", "Drop", "Hat", "Clap", "Kick 2", "Snare", "Tom", "Snap 2" };
const char* const kFilterNames[] = { "LP", "HP", "BP", "Unity BP", "Notch", "Peak", "LP2", "Off" };
const char* const kSyncNames[] = { "Free", "4/1", "2/1", "1/1", "1/2", "1/3", "1/4", "1/6", "1/8", "1/12", "1/16", "1/32" };
const char* const kLfoWaveNames[] = { "Sine", "Tri", "Saw Up", "Saw Dn", "Square", "Random", "Exp Up", "Exp Dn" };
const char* const kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
} // namespace

bool destAllowed(int param)
{
    return param >= 0 && (param < pVelAmount || param == pLfoFreq || param == pLfoAmount || param == pLfo2Freq ||
                          param == pLfo2Amount);
}

const ParamInfo& paramInfo(int p) { return kInfo[clampi(p, 0, kParamCount - 1)]; }
bool paramUsed(int p, int type) { return p >= 0 && p < kParamCount && (kInfo[p].types & (1 << type)) != 0; }

void paramText(int p, int value, char* out, int outLen)
{
    value = clampi(value, 0, paramInfo(p).max);
    switch (p)
    {
        case pWave:
        case pModWave:
        case pWave2:
        case pWave3: snprintf(out, outLen, "%s", kWaveNames[value]); return;
        case pCoarse:
        case pFmFreq:
        case pModF1:
        case pModF2: snprintf(out, outLen, "%s%d", kNoteNames[value % 12], value / 12 - 1); return;
        case pFine: snprintf(out, outLen, "%+d", value - 64); return;
        case pPan:
            if (value == 64)
                snprintf(out, outLen, "C");
            else
                snprintf(out, outLen, "%s%d", value < 64 ? "L" : "R", value < 64 ? 64 - value : value - 64);
            return;
        case pMixMode: snprintf(out, outLen, "%s", value ? "Mix" : "FM"); return;
        case pTransWave: snprintf(out, outLen, "%s", kTransNames[value]); return;
        case pFilterType: snprintf(out, outLen, "%s", kFilterNames[value]); return;
        case pLfoSync:
        case pLfo2Sync: snprintf(out, outLen, "%s", kSyncNames[value]); return;
        case pLfoWave:
        case pLfo2Wave: snprintf(out, outLen, "%s", kLfoWaveNames[value]); return;
        case pVelVolume:
        case pLfoRetrig:
        case pLfo2Retrig: snprintf(out, outLen, "%s", value ? "On" : "Off"); return;
        case pVelDest:
        case pLfoDest:
        case pLfo2Dest:
            if (value == 0)
                snprintf(out, outLen, "None");
            else
                snprintf(out, outLen, "%s", kInfo[value - 1].name);
            return;
        default: snprintf(out, outLen, "%d", value); return;
    }
}

void Engine::setHatOpen(int v, bool open)
{
    if (v >= 0 && v < kVoices)
        m_voice[v].sound.hatOpen = open;
}

// ---- kits ----
int kitCount() { return kKitCount; }
const char* kitName(int kit) { return kKitName[clampi(kit, 0, kKitCount - 1)]; }
bool kitIsFactory(int kit) { return kit < kFactoryKits; }
int slotType(int slot)
{
    return slot <= kSlotD3 ? kDrum : slot == kSlotSnare ? kSnare : slot == kSlotCymbal ? kCymbal : kHihat;
}

void soundFromKit(int kit, int slot, Sound& out)
{
    kit = clampi(kit, 0, kKitCount - 1);
    soundFromKitData(kKit[kit], slot, out);
    snprintf(out.name, sizeof(out.name), "%.7s %s", kKitName[kit], kKitSlotNames[clampi(slot, 0, kKitSlotCount - 1)]);
}

void soundFromKitData(const uint8_t* k, int slot, Sound& out)
{
    slot = clampi(slot, 0, kKitSlotCount - 1);
    const int vi = lxrVoiceOfSlot(slot);
    memset(&out, 0, sizeof(out));
    out.type = (uint8_t)slotType(slot);
    out.hatOpen = slot == kSlotOpenHat;
    for (int p = 0; p <= pVelVolume; p++)
    {
        const int par = parFor(vi, p);
        if (par > 0 && par < kKitParams)
            out.value[p] = k[par];
    }
    out.value[pVelDest] = destFromKit(vi, k[PAR_VEL_DEST_1 + vi]);
    for (int p = 0; p < pLfoFreq; p++)
        if (out.value[p] > kInfo[p].max)
            // the LXR plays a transient number it doesn't have as Snap (transient_setWaveform), and its
            // user-sample waves are not here: Sine
            out.value[p] = p == pWave || p == pModWave || p == pWave2 || p == pWave3 || p == pTransWave ? 0 : kInfo[p].max;
    if (!out.value[pSampleRate])
        out.value[pSampleRate] = 127; // a kit without one plays at full rate

    // the LFOs: the voice's own first, then one of the others that moves this voice
    int lfoCount = 0;
    for (int pass = 0; pass < 2 && lfoCount < 2; pass++)
        for (int j = 0; j < 6 && lfoCount < 2; j++)
        {
            if ((pass == 0) != (j == vi))
                continue;
            const uint8_t dest = destFromKit(vi, k[PAR_TARGET_LFO1 + j]);
            // the own LFO keeps its settings even without a target here; another one only when it moves us
            if (pass == 1 && (!dest || !k[PAR_AMOUNT_LFO1 + j]))
                continue;
            const int b = lfoCount == 0 ? pLfoFreq : pLfo2Freq;
            out.value[b + 0] = k[PAR_FREQ_LFO1 + j];
            out.value[b + 1] = (uint8_t)clampi(k[PAR_SYNC_LFO1 + j], 0, PERC_LFO_SYNCS - 1);
            out.value[b + 2] = k[PAR_AMOUNT_LFO1 + j];
            out.value[b + 3] = (uint8_t)clampi(k[PAR_WAVE_LFO1 + j], 0, PERC_LFO_WAVEFORMS - 1);
            out.value[b + 4] = k[PAR_RETRIGGER_LFO1 + j] != 0;
            out.value[b + 5] = k[PAR_OFFSET_LFO1 + j];
            out.value[b + 6] = dest;
            lfoCount++;
        }
    if (lfoCount < 2)
    {
        out.value[pLfo2Freq] = 64;
        out.value[pLfo2Sync] = 0;
    }
    snprintf(out.name, sizeof(out.name), "LXR %s", kKitSlotNames[slot]);
}

void initSound(Sound& s, int type)
{
    static const uint8_t kSlot[kVoiceTypeCount] = { kSlotD1, kSlotSnare, kSlotCymbal, kSlotClosedHat };
    soundFromKit(0, kSlot[clampi(type, 0, kVoiceTypeCount - 1)], s);
    snprintf(s.name, sizeof(s.name), "%s", kVoiceTypeNames[s.type]);
}

void defaultSound(int track, Sound& s)
{
    switch (track)
    {
        case 0: soundFromKit(0, kSlotD1, s); break;        // Badonka's kick
        case 1: soundFromKit(0, kSlotSnare, s); break;
        case 2: soundFromKit(0, kSlotClosedHat, s); break;
        default:
        {
            // a cowbell: two square waves a fifth-ish apart (C#5, G5, the classic 540 / 800 Hz pair),
            // mixed, through a band-pass, with a short decay
            initSound(s, kDrum);
            static const uint8_t kBell[][2] = {
                { pWave, 3 }, { pCoarse, 73 }, { pFine, 64 }, { pModWave, 3 }, { pFmFreq, 79 }, { pMixMode, 1 },
                { pFmAmount, 64 }, { pAttack, 0 }, { pDecay, 34 }, { pSlope, 64 }, { pPitchAmount, 0 },
                { pPitchDecay, 0 }, { pFilterType, 3 }, { pFilterFreq, 80 }, { pFilterReso, 20 },
                { pFilterDrive, 0 }, { pVolume, 127 }, { pTransVol, 0 }, { pDrive, 0 }, { pSampleRate, 127 },
                { pPan, 64 }, { pVelAmount, 0 }, { pLfoAmount, 0 }, { pLfo2Amount, 0 }, { pVelVolume, 1 },
            };
            for (const auto& b : kBell)
                s.value[b[0]] = b[1];
            snprintf(s.name, sizeof(s.name), "Cowbell");
            break;
        }
    }
}

// ---- the engine ----
void Engine::init()
{
    for (int v = 0; v < kVoices; v++)
    {
        Sound s;
        initSound(s, kDrum);
        setSound(v, s);
        m_voice[v].gain = 1;
    }
}

void Engine::setSound(int v, const Sound& s)
{
    if (v < 0 || v >= kVoices)
        return;
    VoiceState& st = m_voice[v];
    st.sound = s;
    if (st.sound.type >= kVoiceTypeCount)
        st.sound.type = kDrum;
    for (int p = 0; p < kParamCount; p++)
        st.sound.value[p] = (uint8_t)clampi(st.sound.value[p], 0, kInfo[p].max);
    initVoiceDsp(st);
}

void Engine::setValue(int v, int param, int value)
{
    if (v < 0 || v >= kVoices || param < 0 || param >= kParamCount)
        return;
    VoiceState& st = m_voice[v];
    st.sound.value[param] = (uint8_t)clampi(value, 0, kInfo[param].max);
    apply(st, param, st.sound.value[param]);
}

void Engine::trigger(int v, uint8_t velocity, int note)
{
    if (v < 0 || v >= kVoices)
        return;
    VoiceState& st = m_voice[v];
    const uint8_t n = (uint8_t)clampi(note, 0, 127);
    const uint8_t vel = (uint8_t)clampi(velocity, 1, 127);
    st.velocity = vel / 127.f;
    for (int l = 0; l < 2; l++)
        if (st.sound.value[l ? pLfo2Retrig : pLfoRetrig])
            st.lfo[l].phase = st.lfo[l].phaseOffset;
    switch (st.sound.type)
    {
        case kDrum: PercDrum_trigger(&st.dsp.drum, vel, n); break;
        case kSnare: PercSnare_trigger(&st.dsp.snare, vel, n); break;
        case kCymbal: PercCymbal_trigger(&st.dsp.metal, vel, n); break;
        default: PercHat_trigger(&st.dsp.metal, vel, st.sound.hatOpen, n); break;
    }
    st.live = 1;
}

void Engine::stop(int v)
{
    if (v < 0 || v >= kVoices)
        return;
    VoiceState& st = m_voice[v];
    initVoiceDsp(st);
}

void Engine::setTempo(float bpm)
{
    const uint16_t b = (uint16_t)clampi((int)(bpm + 0.5f), 1, 999);
    if (b == lxr_bpm)
        return;
    lxr_bpm = b;
    for (int v = 0; v < kVoices; v++)
        for (int l = 0; l < 2; l++)
            percLfo_recalc(&m_voice[v].lfo[l]);
}

IN_ITCM_RAM void Engine::renderVoice(VoiceState& s, int16_t* buf)
{
    // the modulators, once a block (the LXR's rate): velocity, LFO 1, LFO 2. Each moves its value by
    // amount * (x * 2 - 1) around where the sound has it (the LXR's modNode_updateValue); velocity can
    // move the LFOs' amounts, LFO 1 LFO 2's.
    const uint8_t dest[3] = { s.sound.value[pVelDest], s.sound.value[pLfoDest], s.sound.value[pLfo2Dest] };
    const uint8_t amountParam[3] = { pVelAmount, pLfoAmount, pLfo2Amount };
    float x[3] = { s.velocity, 0, 0 };
    float delta[3] = { 0, 0, 0 }; // what modulator i adds to its value (0..1 of the value's range)
    uint64_t now = 0;
    for (int i = 0; i < 3; i++)
    {
        if (i > 0)
            x[i] = percLfo_calc(&s.lfo[i - 1]);
        float amount = s.sound.value[amountParam[i]] / 127.f;
        for (int j = 0; j < i; j++)
            if (dest[j] == amountParam[i] + 1)
                amount += delta[j];
        amount = amount < 0 ? 0 : amount > 1 ? 1 : amount;
        if (!dest[i] || dest[i] > kParamCount || !destAllowed(dest[i] - 1) || amount <= 0)
            continue;
        delta[i] = amount * (x[i] * 2.f - 1.f);
        now |= 1ull << (dest[i] - 1);
    }
    for (int i = 0; i < 3; i++)
    {
        if (dest[i] == 0 || dest[i] > kParamCount || !(now & (1ull << (dest[i] - 1))))
            continue;
        bool seen = false;
        for (int j = 0; j < i; j++)
            seen |= dest[j] == dest[i];
        const int p = dest[i] - 1;
        if (seen || p == pLfoAmount || p == pLfo2Amount)
            continue; // done above
        const int max = kInfo[p].max;
        float v = s.sound.value[p] / (float)max;
        for (int j = i; j < 3; j++)
            if (dest[j] == dest[i])
                v += delta[j];
        v = v < 0 ? 0 : v > 1 ? 1 : v;
        apply(s, p, (int)(v * max + 0.5f));
    }
    // the values no longer moved go back
    const uint64_t gone = s.modded & ~now;
    if (gone)
        for (int p = 0; p < kParamCount; p++)
            if (gone & (1ull << p))
                apply(s, p, s.sound.value[p]);
    s.modded = now;

    switch (s.sound.type)
    {
        case kDrum:
            PercDrum_calcAsync(&s.dsp.drum);
            PercDrum_calcSyncBlock(&s.dsp.drum, buf, PERC_BLOCK);
            break;
        case kSnare:
            PercSnare_calcAsync(&s.dsp.snare);
            PercSnare_calcSyncBlock(&s.dsp.snare, buf, PERC_BLOCK);
            break;
        case kCymbal:
            PercCymbal_calcAsync(&s.dsp.metal);
            PercCymbal_calcSyncBlock(&s.dsp.metal, buf, PERC_BLOCK);
            break;
        default:
            PercHat_calcAsync(&s.dsp.metal);
            PercHat_calcSyncBlock(&s.dsp.metal, buf, PERC_BLOCK);
            break;
    }
    // the LXR mixer's per-voice sample-rate reducer
    if (s.decimRate < 0.999f)
        for (int i = 0; i < PERC_BLOCK; i++)
        {
            s.decimCount += s.decimRate;
            if (s.decimCount >= 1.f)
            {
                s.decimCount -= 1.f;
                s.decimHold = buf[i];
            }
            buf[i] = s.decimHold;
        }
}

IN_ITCM_RAM void Engine::render(float* left, float* right, int n)
{
    // the LXR's 16-bit voice output, full scale = this much of GibGob's float output
    static constexpr float kOutGain = 0.75f / 32768.f;
    int16_t buf[PERC_BLOCK];
    for (int v = 0; v < kVoices; v++)
    {
        VoiceState& s = m_voice[v];
        if (!s.live)
        {
            // the LFOs run on while the voice rests (free-running ones keep their phase)
            const int blocks = n / PERC_BLOCK;
            for (int b = 0; b < blocks; b++)
                for (int l = 0; l < 2; l++)
                    s.lfo[l].phase += s.lfo[l].phaseInc;
            s.level = 0;
            continue;
        }
        const SlopeEg2* eg = s.sound.type == kDrum ? &s.dsp.drum.oscVolEg : s.sound.type == kSnare ? &s.dsp.snare.oscVolEg : &s.dsp.metal.oscVolEg;
        int peak = 0;
        for (int off = 0; off + PERC_BLOCK <= n; off += PERC_BLOCK)
        {
            if (!s.live)
            {
                for (int l = 0; l < 2; l++)
                    s.lfo[l].phase += s.lfo[l].phaseInc;
                continue;
            }
            renderVoice(s, buf);
            const float g = kOutGain * s.gain;
            const float panL = (s.sound.type == kDrum ? s.dsp.drum.triggeredPanL : s.sound.type == kSnare ? s.dsp.snare.triggeredPanL : s.dsp.metal.triggeredPanL) * g;
            const float panR = (s.sound.type == kDrum ? s.dsp.drum.triggeredPanR : s.sound.type == kSnare ? s.dsp.snare.triggeredPanR : s.dsp.metal.triggeredPanR) * g;
            int blockPeak = 0;
            for (int i = 0; i < PERC_BLOCK; i++)
            {
                const int x = buf[i];
                const int a = x < 0 ? -x : x;
                if (a > blockPeak)
                    blockPeak = a;
                left[off + i] += x * panL;
                right[off + i] += x * panR;
            }
            if (blockPeak > peak)
                peak = blockPeak;
            // quiet: the envelope has ended and nothing came out (the LXR's envelope rests at 0 in its
            // decay stage rather than going back to stopped)
            if ((eg->state == EG_STOPPED || (eg->state == EG_D && eg->value <= 0)) && blockPeak < 2)
                s.live = 0;
        }
        s.level = peak / 32768.f;
    }
}

} // namespace perc
} // namespace gibgob
