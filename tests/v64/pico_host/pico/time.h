// SPDX-License-Identifier: MIT
//
// Host stand-in for the Pico SDK's pico/time.h, backed by the simulated
// microsecond clock in tests/v64/pico_sim.c.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint64_t time_us_64(void);

#ifdef __cplusplus
}
#endif
