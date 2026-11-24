// SPDX-License-Identifier: MIT
#pragma once

#include "io/ByteTransport.h"

#include <cstdint>
#include <deque>
#include <string>

namespace tpc2::test {

class V64Sim;

// ByteTransport that runs the real Pico firmware (firmware/pico/bridge.c and
// parallel.c, compiled for the host) against a simulated V64. Frames written
// by BridgeLink are executed synchronously, so there are no threads and no
// wall-clock waits. The firmware keeps static state, so only one instance may
// be open at a time.
class FakePicoTransport final : public ByteTransport {
public:
    explicit FakePicoTransport(V64Sim& sim) : sim_(sim) {}
    ~FakePicoTransport() override;

    void open() override;
    void close() override;
    [[nodiscard]] bool isOpen() const override { return open_; }
    std::size_t read(uint8_t* out, std::size_t maxBytes, int timeoutMs) override;
    void write(const uint8_t* data, std::size_t size) override;
    [[nodiscard]] std::string description() const override { return "simulated Pico"; }

    // Raw access for corruption tests.
    void injectToFirmware(const uint8_t* data, std::size_t size);
    [[nodiscard]] std::size_t framesHandled() const { return writes_; }

    void deliver(const uint8_t* data, std::size_t size);

private:
    V64Sim& sim_;
    bool open_ = false;
    std::deque<uint8_t> toHost_;
    std::size_t writes_ = 0;
};

} // namespace tpc2::test
