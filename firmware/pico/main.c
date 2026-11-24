// SPDX-License-Identifier: MIT
//
// TPC2 Next USB bridge firmware for the Raspberry Pi Pico.
//
// The Pico owns the time-critical Doctor V64 parallel handshake and exposes a
// small framed serial protocol (SLIP + CRC-16) to the host. See docs/PROTOCOL.md
// for the wire format and firmware/pico/README.md for the wiring.

#include "board.h"
#include "bridge.h"
#include "parallel.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/time.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void bridge_platform_write(const uint8_t* data, size_t length)
{
    fwrite(data, 1, length, stdout);
    fflush(stdout);

#if TPC2_PIN_LED
    gpio_xor_mask(1u << TPC2_PIN_LED);
#endif
}

int main(void)
{
    stdio_init_all();
    // The framed protocol is binary: never translate 0x0A to CRLF.
    stdio_set_translate_crlf(&stdio_usb, false);

    static char stdout_buffer[16384];
    setvbuf(stdout, stdout_buffer, _IOFBF, sizeof stdout_buffer);

    parallel_init();

    for (;;) {
        const int byte = getchar_timeout_us(1000);
        if (byte >= 0)
            bridge_receive_byte((uint8_t)byte);
    }
}
