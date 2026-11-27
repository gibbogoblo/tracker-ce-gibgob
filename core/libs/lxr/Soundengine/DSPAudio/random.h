/*
 * random.h
 *
 *  Created on: 07.04.2012
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */


#ifndef RANDOM_H_
#define RANDOM_H_

#include "lxr/stm32f7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif
uint32_t GetRngValue(void); /* GibGob OS: a xorshift32 (lxr_glue.c) */
void lxr_seedRandom(uint32_t seed);
#ifdef __cplusplus
}
#endif

#endif /* RANDOM_H_ */
