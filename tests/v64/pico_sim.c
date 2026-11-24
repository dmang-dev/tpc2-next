// SPDX-License-Identifier: MIT
//
// GPIO and timer simulation for running firmware/pico/parallel.c on the host.
// Pin numbers come from firmware/pico/board.h, so a custom pin map is tested
// exactly as it would be built. The DB25 levels seen by the V64 are derived
// from the simulated pad state, including the open-drain AUTOFD/SELECTIN
// lines and their external pull-ups.

#include "pico_sim.h"

#include "board.h"
#include "hardware/gpio.h"
#include "pico/time.h"

static v64_bus* bus_;
static uint32_t out_value_;
static uint32_t out_enable_;
static uint64_t standalone_us_;

void pico_sim_attach(v64_bus* bus)
{
    bus_ = bus;
    out_value_ = 0;
    out_enable_ = 0;
}

static void tick(void)
{
    if (bus_)
        v64_bus_tick(bus_, 1);
    else
        ++standalone_us_;
}

static uint8_t level(uint pin)
{
    return (uint8_t)((out_value_ >> pin) & 1u);
}

static uint8_t driven_low(uint pin)
{
    return (uint8_t)(((out_enable_ >> pin) & 1u) && !level(pin));
}

static void update_cable(void)
{
    if (!bus_)
        return;
    v64_host_pins pins = bus_->host;
    const uint32_t data_mask = 0xFFu << TPC2_PIN_DATA_BASE;
    pins.data = (out_enable_ & data_mask) == data_mask
        ? (uint8_t)(out_value_ >> TPC2_PIN_DATA_BASE)
        : 0x00; // only before parallel_init() configures the data pins
    pins.nstrobe = ((out_enable_ >> TPC2_PIN_STROBE) & 1u) ? level(TPC2_PIN_STROBE) : 1;
    pins.ninit = ((out_enable_ >> TPC2_PIN_INIT) & 1u) ? level(TPC2_PIN_INIT) : 1;
    pins.autofd_low = driven_low(TPC2_PIN_AUTOFD);
    pins.selectin_low = driven_low(TPC2_PIN_SELECTIN);
    v64_bus_set_host(bus_, &pins);
}

void gpio_init(uint pin)
{
    out_enable_ &= ~(1u << pin);
    out_value_ &= ~(1u << pin);
    update_cable();
}

void gpio_set_dir(uint pin, bool out)
{
    if (out)
        out_enable_ |= 1u << pin;
    else
        out_enable_ &= ~(1u << pin);
    update_cable();
}

void gpio_put(uint pin, bool value)
{
    if (value)
        out_value_ |= 1u << pin;
    else
        out_value_ &= ~(1u << pin);
    update_cable();
}

void gpio_put_masked(uint32_t mask, uint32_t value)
{
    out_value_ = (out_value_ & ~mask) | (value & mask);
    update_cable();
}

void gpio_xor_mask(uint32_t mask)
{
    out_value_ ^= mask;
    update_cable();
}

void gpio_pull_up(uint pin)
{
    (void)pin; // every simulated input idles high
}

bool gpio_get(uint pin)
{
    tick();
    if (!bus_)
        return true;
    const v64_dev_pins* d = &bus_->visible;
    const v64_host_pins* h = &bus_->host;
    if (pin == TPC2_PIN_BUSY)
        return d->busy;
    if (pin == TPC2_PIN_ACK)
        return d->nack;
    if (pin == TPC2_PIN_PE)
        return d->pe;
    if (pin == TPC2_PIN_SLCT)
        return d->slct;
    if (pin == TPC2_PIN_ERROR)
        return d->nerror;
    if (pin == TPC2_PIN_AUTOFD)
        return v64_line_autofd(h, d);
    if (pin == TPC2_PIN_SELECTIN)
        return v64_line_selectin(h, d);
    return level(pin);
}

uint64_t time_us_64(void)
{
    tick();
    return bus_ ? bus_->now_us : standalone_us_;
}
