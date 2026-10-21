// SPDX-License-Identifier: MIT
#pragma once

#include <stddef.h>
#include <stdint.h>

enum {
    V64_PARALLEL_OK = 0,
    V64_PARALLEL_TIMEOUT = 1,
    V64_PARALLEL_PROTOCOL = 2,
};

enum {
    V64_MODE_OLD = 0,
    V64_MODE_NEW = 1,
};

void parallel_init(void);
void parallel_reset(void);

// preferred: 0 = auto, 1 = old only, 2 = new only.
// Returns V64_MODE_OLD, V64_MODE_NEW, or -1 on timeout.
int parallel_sync(int preferred);
int parallel_mode(void);

int parallel_write(const uint8_t* data, size_t length);
int parallel_read(uint8_t* out, size_t length);
int parallel_check_sync(void);

// Exposed for diagnostics and unit testing of the register emulation.
void parallel_write_data(uint8_t value);
uint8_t parallel_read_status(void);
void parallel_write_control(uint8_t value);
uint8_t parallel_read_control(void);
