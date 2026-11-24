// SPDX-License-Identifier: MIT
#pragma once

#include <stddef.h>
#include <stdint.h>

#define BRIDGE_VERSION "tpc2-pico 1.0.0"

// Feeds one byte received from the host. Complete, CRC-checked frames are
// executed immediately and answered through bridge_platform_write().
void bridge_receive_byte(uint8_t byte);

// Drops any partial frame and restores the default (auto) handshake mode.
void bridge_reset(void);

// Provided by the platform: main.c on the Pico, the simulator in the tests.
void bridge_platform_write(const uint8_t* data, size_t length);
