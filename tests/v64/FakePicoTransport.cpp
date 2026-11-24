// SPDX-License-Identifier: MIT
#include "FakePicoTransport.h"

#include "V64Sim.h"
#include "core/Error.h"

extern "C" {
#include "bridge.h"
#include "parallel.h"
#include "pico_sim.h"
}

namespace tpc2::test {
namespace {

FakePicoTransport* active = nullptr;

} // namespace

FakePicoTransport::~FakePicoTransport()
{
    close();
}

void FakePicoTransport::open()
{
    if (active && active != this)
        throw Error(ErrorCode::Io, "Another simulated Pico is already open");
    active = this;
    pico_sim_attach(&sim_.bus());
    parallel_init(); // what the firmware does at power-up
    bridge_reset();
    toHost_.clear();
    open_ = true;
}

void FakePicoTransport::close()
{
    if (active == this) {
        pico_sim_attach(nullptr);
        active = nullptr;
    }
    open_ = false;
}

std::size_t FakePicoTransport::read(uint8_t* out, std::size_t maxBytes, int timeoutMs)
{
    (void)timeoutMs; // every reply was produced synchronously by write()
    std::size_t count = 0;
    while (count < maxBytes && !toHost_.empty()) {
        out[count++] = toHost_.front();
        toHost_.pop_front();
    }
    return count;
}

void FakePicoTransport::write(const uint8_t* data, std::size_t size)
{
    if (!open_)
        throw Error(ErrorCode::NotConnected, "Simulated Pico is not open");
    ++writes_;
    injectToFirmware(data, size);
}

void FakePicoTransport::injectToFirmware(const uint8_t* data, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i)
        bridge_receive_byte(data[i]);
}

void FakePicoTransport::deliver(const uint8_t* data, std::size_t size)
{
    toHost_.insert(toHost_.end(), data, data + size);
}

} // namespace tpc2::test

extern "C" void bridge_platform_write(const uint8_t* data, size_t length)
{
    if (tpc2::test::active)
        tpc2::test::active->deliver(data, length);
}
