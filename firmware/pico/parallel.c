// SPDX-License-Identifier: MIT
#include "parallel.h"

#include "board.h"

#include "hardware/gpio.h"
#include "pico/time.h"

#include <stdbool.h>

// SPP register emulation. The host software in src/link/ParallelLink.cpp was
// written against a real PC parallel port, so the bridge has to reproduce the
// register bit layout (including the inverting buffers) rather than expose raw
// pin levels.

#define CTRL_STROBE 0x01
#define CTRL_AUTOFD 0x02
#define CTRL_INIT 0x04
#define CTRL_SELECTIN 0x08

#define STATUS_ERROR 0x08
#define STATUS_SLCT 0x10
#define STATUS_PE 0x20
#define STATUS_ACK 0x40
#define STATUS_BUSY 0x80

#define HANDSHAKE_TIMEOUT_US (5000ull * 1000ull)
#define BYTE_TIMEOUT_US (1500ull * 1000ull)

static int mode_ = V64_MODE_OLD;
static uint8_t control_shadow_ = CTRL_INIT;

static inline uint32_t data_mask(void)
{
    return 0xFFu << TPC2_PIN_DATA_BASE;
}

static inline void data_write(uint8_t value)
{
    gpio_put_masked(data_mask(),
                    ((uint32_t)value << TPC2_PIN_DATA_BASE) & data_mask());
}

static inline void open_drain(uint pin, bool low)
{
    if (low) {
        gpio_put(pin, 0);
        gpio_set_dir(pin, GPIO_OUT);
    } else {
        // Released; the external 4.7 kOhm pull-up takes the line high and the
        // V64 is allowed to pull it low for the handshake.
        gpio_set_dir(pin, GPIO_IN);
    }
}

void parallel_write_data(uint8_t value)
{
    data_write(value);
}

uint8_t parallel_read_status(void)
{
    uint8_t value = 0;
    if (!gpio_get(TPC2_PIN_BUSY))
        value |= STATUS_BUSY;
    if (!gpio_get(TPC2_PIN_ACK))
        value |= STATUS_ACK;
    if (gpio_get(TPC2_PIN_PE))
        value |= STATUS_PE;
    if (gpio_get(TPC2_PIN_SLCT))
        value |= STATUS_SLCT;
    if (!gpio_get(TPC2_PIN_ERROR))
        value |= STATUS_ERROR;
    return value;
}

void parallel_write_control(uint8_t value)
{
    control_shadow_ = (uint8_t)(value & 0x0F);

    // STROBE and INIT are push-pull outputs. AUTOFD and SELECTIN are open
    // drain because the V64 drives them during the old handshake.
    gpio_put(TPC2_PIN_STROBE, (control_shadow_ & CTRL_STROBE) == 0);
    gpio_put(TPC2_PIN_INIT, (control_shadow_ & CTRL_INIT) != 0);
    open_drain(TPC2_PIN_AUTOFD, (control_shadow_ & CTRL_AUTOFD) != 0);
    open_drain(TPC2_PIN_SELECTIN, (control_shadow_ & CTRL_SELECTIN) != 0);
}

uint8_t parallel_read_control(void)
{
    uint8_t value = (uint8_t)(control_shadow_ & (CTRL_STROBE | CTRL_INIT));
    // On a PC these two bits read back the actual (inverted) line level, which
    // is how the V64 acknowledges the old-mode handshake.
    if (!gpio_get(TPC2_PIN_AUTOFD))
        value |= CTRL_AUTOFD;
    if (!gpio_get(TPC2_PIN_SELECTIN))
        value |= CTRL_SELECTIN;
    return value;
}

void parallel_init(void)
{
    for (unsigned bit = 0; bit < 8; ++bit) {
        const unsigned pin = TPC2_PIN_DATA_BASE + bit;
        gpio_init(pin);
        gpio_put(pin, 0);
        gpio_set_dir(pin, GPIO_OUT);
    }

    gpio_init(TPC2_PIN_STROBE);
    gpio_init(TPC2_PIN_INIT);
    gpio_init(TPC2_PIN_AUTOFD);
    gpio_init(TPC2_PIN_SELECTIN);

    gpio_pull_up(TPC2_PIN_AUTOFD);
    gpio_pull_up(TPC2_PIN_SELECTIN);

    gpio_init(TPC2_PIN_BUSY);
    gpio_init(TPC2_PIN_ACK);
    gpio_init(TPC2_PIN_PE);
    gpio_init(TPC2_PIN_SLCT);
    gpio_init(TPC2_PIN_ERROR);
    gpio_set_dir(TPC2_PIN_BUSY, GPIO_IN);
    gpio_set_dir(TPC2_PIN_ACK, GPIO_IN);
    gpio_set_dir(TPC2_PIN_PE, GPIO_IN);
    gpio_set_dir(TPC2_PIN_SLCT, GPIO_IN);
    gpio_set_dir(TPC2_PIN_ERROR, GPIO_IN);
    gpio_pull_up(TPC2_PIN_BUSY);
    gpio_pull_up(TPC2_PIN_ACK);
    gpio_pull_up(TPC2_PIN_PE);
    gpio_pull_up(TPC2_PIN_SLCT);
    gpio_pull_up(TPC2_PIN_ERROR);

#if TPC2_PIN_LED
    gpio_init(TPC2_PIN_LED);
    gpio_set_dir(TPC2_PIN_LED, GPIO_OUT);
    gpio_put(TPC2_PIN_LED, 1);
#endif

    parallel_reset();
}

void parallel_reset(void)
{
    parallel_write_control(CTRL_INIT); // idle: all control lines inactive
    parallel_write_data(0x00);
}

int parallel_mode(void)
{
    return mode_;
}

static bool wait_control(unsigned bit, bool level, uint64_t timeout_us)
{
    const uint64_t deadline = time_us_64() + timeout_us;
    while (time_us_64() < deadline) {
        if (((parallel_read_control() >> bit) & 1u) == (level ? 1u : 0u))
            return true;
    }
    return false;
}

static bool wait_status(unsigned bit, bool level, uint64_t timeout_us)
{
    const uint64_t deadline = time_us_64() + timeout_us;
    while (time_us_64() < deadline) {
        if (((parallel_read_status() >> bit) & 1u) == (level ? 1u : 0u))
            return true;
    }
    return false;
}

static bool sync_old(void)
{
    // The original BIOS uses SLCT_IN (control bit 3) to acknowledge the
    // 0xAA/0x55 pulse pairs.
    if (!wait_control(3, false, HANDSHAKE_TIMEOUT_US))
        return false;

    parallel_write_data(0xAA);
    parallel_write_control(0x00);
    if (!wait_control(3, true, HANDSHAKE_TIMEOUT_US))
        return false;
    parallel_write_control(CTRL_INIT);
    if (!wait_control(3, false, HANDSHAKE_TIMEOUT_US))
        return false;

    parallel_write_data(0x55);
    parallel_write_control(0x00);
    if (!wait_control(3, true, HANDSHAKE_TIMEOUT_US))
        return false;
    parallel_write_control(CTRL_INIT);
    if (!wait_control(3, false, HANDSHAKE_TIMEOUT_US))
        return false;

    return true;
}

static bool sync_new(void)
{
    // BIOS 1.2x and later acknowledge on nERROR (status bit 3).
    if (!wait_status(3, true, HANDSHAKE_TIMEOUT_US))
        return false;

    parallel_write_data(0xA5);
    parallel_write_control(0x00);
    if (!wait_status(3, false, HANDSHAKE_TIMEOUT_US))
        return false;
    parallel_write_control(CTRL_INIT);
    if (!wait_status(3, true, HANDSHAKE_TIMEOUT_US))
        return false;

    parallel_write_data(0x5A);
    parallel_write_control(0x00);
    if (!wait_status(3, false, HANDSHAKE_TIMEOUT_US))
        return false;
    parallel_write_control(CTRL_INIT);
    if (!wait_status(3, true, HANDSHAKE_TIMEOUT_US))
        return false;

    return true;
}

int parallel_sync(int preferred)
{
    parallel_write_control(CTRL_INIT);
    parallel_write_data(0x00);

    const bool try_old = preferred == 0 || preferred == 1;
    const bool try_new = preferred == 0 || preferred == 2;

    if (try_old && sync_old()) {
        mode_ = V64_MODE_OLD;
        return mode_;
    }

    parallel_reset();
    if (try_new && sync_new()) {
        mode_ = V64_MODE_NEW;
        return mode_;
    }

    parallel_reset();
    return -1;
}

static bool wait_ready_even(void)
{
    if (mode_ == V64_MODE_OLD)
        return wait_control(1, false, BYTE_TIMEOUT_US); // AUTOFD released
    return wait_status(4, true, BYTE_TIMEOUT_US);       // SLCT high
}

static bool wait_ready_odd(void)
{
    if (mode_ == V64_MODE_OLD)
        return wait_control(1, true, BYTE_TIMEOUT_US); // AUTOFD asserted
    return wait_status(4, false, BYTE_TIMEOUT_US);      // SLCT low
}

int parallel_write(const uint8_t* data, size_t length)
{
    // TPC keeps the even/odd phase in the STB bit of the control register.
    // Derive it at the start of every call: BridgeLink splits large writes into
    // several 8 KiB commands, and a preceding read may have left the phase in
    // the other state.
    bool odd = (parallel_read_control() & CTRL_STROBE) != 0;
    for (size_t i = 0; i < length; ++i) {
        if (!odd) {
            if (!wait_ready_even())
                return V64_PARALLEL_TIMEOUT;
            parallel_write_data(data[i]);
            parallel_write_control(CTRL_INIT | CTRL_STROBE);
            odd = true;
        } else {
            if (!wait_ready_odd())
                return V64_PARALLEL_TIMEOUT;
            parallel_write_data(data[i]);
            parallel_write_control(CTRL_INIT);
            odd = false;
        }
    }
    return V64_PARALLEL_OK;
}

int parallel_read(uint8_t* out, size_t length)
{
    // D7 high selects the read direction; the V64 returns the low nibble
    // first, then the high nibble, toggling BUSY around each one.
    parallel_write_data(0x80);
    for (size_t i = 0; i < length; ++i) {
        if (!wait_status(7, true, BYTE_TIMEOUT_US))
            return V64_PARALLEL_TIMEOUT;
        const uint8_t low = (uint8_t)((parallel_read_status() >> 3) & 0x0F);

        parallel_write_data(0x00);
        if (!wait_status(7, false, BYTE_TIMEOUT_US))
            return V64_PARALLEL_TIMEOUT;
        const uint8_t high = (uint8_t)((parallel_read_status() >> 3) & 0x0F);

        parallel_write_data(0x80);
        out[i] = (uint8_t)(low | (high << 4));
    }
    return V64_PARALLEL_OK;
}

int parallel_check_sync(void)
{
    const uint64_t deadline = time_us_64() + HANDSHAKE_TIMEOUT_US;
    while (time_us_64() < deadline) {
        const uint8_t control = parallel_read_control();
        const uint8_t status = parallel_read_status();
        bool ready;
        if (mode_ == V64_MODE_OLD) {
            const uint8_t low = (uint8_t)(control & 0x03);
            ready = low == 0x03 || low == 0x00;
        } else {
            ready = (((control & CTRL_STROBE)
                      ^ ((status & STATUS_SLCT) >> 4)) != 0);
        }

        if (ready) {
            parallel_write_data(0x00);
            if (wait_status(7, false, HANDSHAKE_TIMEOUT_US))
                return V64_PARALLEL_OK;
            return V64_PARALLEL_TIMEOUT;
        }
    }
    return V64_PARALLEL_TIMEOUT;
}
