/* SPDX-License-Identifier: GPL-3.0-only
 * GibGob OS: what the LXR-02's DSP sources (Erica Synths, GPL-3.0-only) need of the STM32 HAL / CMSIS,
 * as plain C so they build both for the Mini and for the host tests. After Elixir's host shim
 * (github.com/dmajka/elixir, tests/lxrref/shim, GPL-3.0-only). */
#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#define I2S_AUDIOFREQ_44K 44100u
static inline int32_t lxr_ssat(int32_t v, int b) { int32_t m = 1 << (b - 1); return v > m - 1 ? m - 1 : v < -m ? -m : v; }
#define __SSAT(v, b) lxr_ssat((int32_t)(v), (b))
static inline uint32_t lxr_qadd16(uint32_t a, uint32_t b)
{
    int32_t lo = lxr_ssat((int16_t)a + (int16_t)b, 16), hi = lxr_ssat((int16_t)(a >> 16) + (int16_t)(b >> 16), 16);
    return (uint16_t)lo | (uint32_t)(uint16_t)hi << 16;
}
static inline uint32_t lxr_qsub16(uint32_t a, uint32_t b)
{
    int32_t lo = lxr_ssat((int16_t)a - (int16_t)b, 16), hi = lxr_ssat((int16_t)(a >> 16) - (int16_t)(b >> 16), 16);
    return (uint16_t)lo | (uint32_t)(uint16_t)hi << 16;
}
#define __QADD16(a, b) lxr_qadd16((a), (b))
#define __QSUB16(a, b) lxr_qsub16((a), (b))
#define __PKHBT(a, b, s) (((uint32_t)(a) & 0xFFFFu) | (((uint32_t)(b) << (s)) & 0xFFFF0000u))
#define __CLZ(v) ((v) ? (uint8_t)__builtin_clz(v) : 32)
