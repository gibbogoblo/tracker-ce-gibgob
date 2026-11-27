/* SPDX-License-Identifier: GPL-3.0-only
 * GibGob OS: the few functions of the LXR-02 firmware (Erica Synths, GPL-3.0-only) that its DSP calls
 * outside the DSP sources: the random numbers, the oscillators' fine tune, the tempo. */
#include <stdint.h>
#include "lxr/Soundengine/DSPAudio/random.h"
#include "lxr/Soundengine/MIDI/MidiParser.h"
#include "lxr/Soundengine/Sequencer/sequencer.h"

static uint32_t s_rng = 0x9E3779B9u;
uint32_t GetRngValue(void) /* a xorshift32: the LXR's hardware RNG's job */
{
    uint32_t x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return x;
}
void lxr_seedRandom(uint32_t seed) /* the host tests */
{
    s_rng = seed ? seed : 0x9E3779B9u;
}

#define SEMITONE_UP 1.0594630943592952645618252949463f
/* MidiParser.c: a 0..127 value as +-50 cents */
float midiParser_calcDetune(uint8_t value)
{
    return 1.f + (value / 127.f - 0.5f) * (SEMITONE_UP - 1.f);
}

/* the tempo, set by GibGob's engine (synced LFOs) */
volatile uint16_t lxr_bpm = 120;
uint16_t seq_getBpm(void)
{
    return lxr_bpm;
}
