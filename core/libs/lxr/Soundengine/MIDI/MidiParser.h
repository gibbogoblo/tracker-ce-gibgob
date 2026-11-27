/* SPDX-License-Identifier: GPL-3.0-only (what the LXR-02's DSP uses of MidiParser.h) */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
float midiParser_calcDetune(uint8_t value);
#ifdef __cplusplus
}
#endif
