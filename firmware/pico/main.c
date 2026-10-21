// SPDX-License-Identifier: MIT
//
// TPC2 Next USB bridge firmware for the Raspberry Pi Pico.
//
// The Pico owns the time-critical Doctor V64 parallel handshake and exposes a
// small framed serial protocol (SLIP + CRC-16) to the host. See docs/PROTOCOL.md
// for the wire format and firmware/pico/README.md for the wiring.

#include "board.h"
#include "parallel.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/time.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BRIDGE_VERSION "tpc2-pico 1.0.0"

enum bridge_command {
    CMD_PING = 0x01,
    CMD_SYNC = 0x02,
    CMD_SET_MODE = 0x03,
    CMD_WRITE = 0x04,
    CMD_READ = 0x05,
    CMD_CHECK_SYNC = 0x06,
    CMD_RESET = 0x07,
    CMD_INFO = 0x08,
};

enum bridge_status {
    STATUS_OK = 0,
    STATUS_TIMEOUT = 1,
    STATUS_PROTOCOL = 2,
    STATUS_IO = 3,
    STATUS_UNSUPPORTED = 4,
    STATUS_CANCELLED = 5,
    STATUS_BAD_REQUEST = 6,
};

#define SLIP_END 0xC0
#define SLIP_ESC 0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

#define MAX_PAYLOAD 8192
#define MAX_BODY (MAX_PAYLOAD + 7)
#define MAX_ENCODED (MAX_BODY * 2 + 1)

static uint8_t rx_body[MAX_BODY];
static size_t rx_length;
static bool rx_escape;
static bool rx_overflow;

static uint8_t io_buffer[MAX_PAYLOAD];
static uint8_t tx_buffer[MAX_ENCODED];
static int preferred_mode = 0; // 0 auto, 1 old, 2 new

static uint16_t crc16(const uint8_t* data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
    }
    return crc;
}

static size_t slip_append(size_t offset, uint8_t value)
{
    if (value == SLIP_END) {
        tx_buffer[offset++] = SLIP_ESC;
        tx_buffer[offset++] = SLIP_ESC_END;
    } else if (value == SLIP_ESC) {
        tx_buffer[offset++] = SLIP_ESC;
        tx_buffer[offset++] = SLIP_ESC_ESC;
    } else {
        tx_buffer[offset++] = value;
    }
    return offset;
}

static void send_frame(uint8_t command, uint8_t sequence, uint8_t status,
                       const uint8_t* data, size_t length)
{
    if (length > MAX_PAYLOAD)
        length = 0;

    const uint8_t header[5] = {
        (uint8_t)(command | 0x80),
        sequence,
        status,
        (uint8_t)(length & 0xFF),
        (uint8_t)((length >> 8) & 0xFF),
    };

    uint16_t crc = 0xFFFF;
    // Inline CRC-16/CCITT-FALSE over header and payload.
    for (size_t i = 0; i < sizeof header; ++i) {
        crc ^= (uint16_t)header[i] << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
    }
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
    }

    size_t out = 0;
    for (size_t i = 0; i < sizeof header; ++i)
        out = slip_append(out, header[i]);
    for (size_t i = 0; i < length; ++i)
        out = slip_append(out, data[i]);
    out = slip_append(out, (uint8_t)(crc & 0xFF));
    out = slip_append(out, (uint8_t)(crc >> 8));
    tx_buffer[out++] = SLIP_END;

    fwrite(tx_buffer, 1, out, stdout);
    fflush(stdout);

#if TPC2_PIN_LED
    gpio_xor_mask(1u << TPC2_PIN_LED);
#endif
}

static void send_status(uint8_t sequence, uint8_t status, const char* message)
{
    if (message == NULL)
        message = "";
    send_frame(0, sequence, status, (const uint8_t*)message, strlen(message));
}

static void handle_frame(uint8_t command, uint8_t sequence,
                         const uint8_t* data, size_t length)
{
    switch (command) {
    case CMD_PING:
        send_status(sequence, STATUS_OK, BRIDGE_VERSION);
        break;

    case CMD_SYNC: {
        if (length < 1) {
            send_status(sequence, STATUS_BAD_REQUEST, "Sync needs a mode byte");
            break;
        }
        preferred_mode = data[0];
        const int mode = parallel_sync(preferred_mode);
        if (mode < 0) {
            send_status(sequence, STATUS_TIMEOUT,
                        "No answer from the V64 (check power, cable and the "
                        "front-panel transfer screen)");
            break;
        }
        const uint8_t result = (uint8_t)(mode == V64_MODE_NEW ? 1 : 0);
        send_frame(0, sequence, STATUS_OK, &result, 1);
        break;
    }

    case CMD_SET_MODE:
        if (length < 1) {
            send_status(sequence, STATUS_BAD_REQUEST, "SetMode needs a mode byte");
            break;
        }
        preferred_mode = data[0];
        send_status(sequence, STATUS_OK, NULL);
        break;

    case CMD_WRITE:
        if (length > MAX_PAYLOAD) {
            send_status(sequence, STATUS_BAD_REQUEST, "Write payload is too large");
            break;
        }
        if (parallel_write(data, length) != V64_PARALLEL_OK) {
            send_status(sequence, STATUS_TIMEOUT,
                        "The V64 stopped accepting data");
            break;
        }
        send_status(sequence, STATUS_OK, NULL);
        break;

    case CMD_READ: {
        if (length != 4) {
            send_status(sequence, STATUS_BAD_REQUEST,
                        "Read needs a 32-bit little-endian length");
            break;
        }
        const uint32_t count = (uint32_t)data[0]
            | ((uint32_t)data[1] << 8)
            | ((uint32_t)data[2] << 16)
            | ((uint32_t)data[3] << 24);
        if (count == 0 || count > MAX_PAYLOAD) {
            send_status(sequence, STATUS_BAD_REQUEST, "Invalid read length");
            break;
        }
        if (parallel_read(io_buffer, count) != V64_PARALLEL_OK) {
            send_status(sequence, STATUS_TIMEOUT,
                        "The V64 stopped sending data");
            break;
        }
        send_frame(0, sequence, STATUS_OK, io_buffer, count);
        break;
    }

    case CMD_CHECK_SYNC:
        if (parallel_check_sync() != V64_PARALLEL_OK) {
            send_status(sequence, STATUS_TIMEOUT,
                        "The V64 did not acknowledge the command header");
            break;
        }
        send_status(sequence, STATUS_OK, NULL);
        break;

    case CMD_RESET:
        parallel_reset();
        send_status(sequence, STATUS_OK, NULL);
        break;

    case CMD_INFO: {
        char info[192];
        snprintf(info, sizeof info,
                 "%s\nmode=%s\npins: data=%d ctrl=%d,%d,%d,%d status=%d,%d,%d,%d,%d",
                 BRIDGE_VERSION,
                 parallel_mode() == V64_MODE_NEW ? "new" : "old",
                 TPC2_PIN_DATA_BASE,
                 TPC2_PIN_STROBE, TPC2_PIN_AUTOFD, TPC2_PIN_INIT,
                 TPC2_PIN_SELECTIN,
                 TPC2_PIN_BUSY, TPC2_PIN_ACK, TPC2_PIN_PE,
                 TPC2_PIN_SLCT, TPC2_PIN_ERROR);
        send_frame(0, sequence, STATUS_OK, (const uint8_t*)info, strlen(info));
        break;
    }

    default:
        send_status(sequence, STATUS_UNSUPPORTED, "Unknown bridge command");
        break;
    }
}

static void receive_byte(uint8_t byte)
{
    if (byte == SLIP_END) {
        if (rx_length >= 7 && !rx_overflow) {
            const uint16_t claimed =
                (uint16_t)rx_body[rx_length - 2]
                | ((uint16_t)rx_body[rx_length - 1] << 8);
            const uint16_t actual = crc16(rx_body, rx_length - 2);
            const size_t length =
                (size_t)rx_body[3] | ((size_t)rx_body[4] << 8);

            if (claimed == actual && rx_length == length + 7 && length <= MAX_PAYLOAD) {
                handle_frame((uint8_t)(rx_body[0] & 0x7F), rx_body[1],
                             rx_body + 5, length);
            }
        }
        rx_length = 0;
        rx_escape = false;
        rx_overflow = false;
        return;
    }

    if (rx_escape) {
        if (byte == SLIP_ESC_END)
            byte = SLIP_END;
        else if (byte == SLIP_ESC_ESC)
            byte = SLIP_ESC;
        else {
            rx_overflow = true;
            rx_escape = false;
            return;
        }
        rx_escape = false;
    } else if (byte == SLIP_ESC) {
        rx_escape = true;
        return;
    }

    if (rx_length < sizeof rx_body)
        rx_body[rx_length++] = byte;
    else
        rx_overflow = true;
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
            receive_byte((uint8_t)byte);
    }
}
