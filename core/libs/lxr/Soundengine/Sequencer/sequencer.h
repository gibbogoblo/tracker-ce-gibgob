/* SPDX-License-Identifier: GPL-3.0-only (what the LXR-02's DSP uses of sequencer.h) */
#pragma once
#include <stdint.h>
#define SEQ_DEFAULT_NOTE 60
#ifdef __cplusplus
extern "C" {
#endif
uint16_t seq_getBpm(void);
#ifdef __cplusplus
}
#endif
