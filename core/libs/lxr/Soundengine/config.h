/* SPDX-License-Identifier: GPL-3.0-only
 * GibGob OS: the LXR-02's Soundengine/config.h (Erica Synths, GPL-3.0-only), the parts its DSP uses,
 * at the Mini's 44.1 kHz. */
#pragma once
#include "lxr/stm32f7xx_hal.h"
#define USE_BIG_SAMPLE_BUFFER 1
#define SMOOTH_MIXER_VOL_PAN_DRIVE 1
#define OUTPUT_DMA_SIZE 64
#define UNIT_GAIN_DRIVE 0
#define USE_FILTER_DRIVE 0
#define AMP_EG_SYNC 0
#define ENABLE_MIX_OSC 1
#define ENABLE_DRUM_SVF 1
#define PITCH_AMOUNT_FACTOR 32
#define INTERPOLATE_OSC 1
#define INTERPOLATE_FM_OSC 1
#define SYNTH_FS I2S_AUDIOFREQ_44K
#define REAL_FS (float)44100.f
#define REAL_FS2 (REAL_FS / 2.f)
#define NUM_DRUM_VOICES 3
#define EG_SPEED 1;
#define INDTCM
#define INDTCMZ
/* the hot DSP code: in the Mini's fast instruction RAM, with its other audio code (config/config.h's
   AUDIO_CORE_RAM); on a computer (the tests) nowhere special */
#if defined(PLATFORM_IMX)
#define IN_ITCM_RAM __attribute__((section(".audio_ram_functions")))
#else
#define IN_ITCM_RAM
#endif
