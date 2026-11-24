// SPDX-License-Identifier: MIT
//
// Host stand-in for the Pico SDK's hardware/gpio.h. Only the calls used by
// firmware/pico/parallel.c and main.c are provided; they drive the simulated
// V64 in tests/v64/pico_sim.c.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int uint;

#define GPIO_IN false
#define GPIO_OUT true

void gpio_init(uint pin);
void gpio_set_dir(uint pin, bool out);
void gpio_put(uint pin, bool value);
bool gpio_get(uint pin);
void gpio_put_masked(uint32_t mask, uint32_t value);
void gpio_xor_mask(uint32_t mask);
void gpio_pull_up(uint pin);

#ifdef __cplusplus
}
#endif
