// SPDX-License-Identifier: MIT
#pragma once

#include "core/Link.h"
#include "platform/ParallelPort.h"

#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace tpc2 {

// Bit-banged SPP implementation of the protocol used by TPC 2.0. It is kept
// for legacy PCs with a real parallel port; the USB bridge implements the same
// protocol on a microcontroller and is the recommended path on modern systems.
class ParallelLink final : public Link {
public:
    ParallelLink(std::unique_ptr<ParallelPort> port, uint16_t baseAddress);

    void open() override;
    void close() override;
    [[nodiscard]] bool isOpen() const override;

    ComMode sync() override;
    void checkSync() override;
    void write(std::span<const uint8_t> data) override;
    void read(std::span<uint8_t> out) override;

    [[nodiscard]] ComMode mode() const override { return mode_; }
    void setPreferredMode(std::optional<ComMode> mode) override { preferred_ = mode; }
    void reset() override;

    [[nodiscard]] std::string description() const override;

    void setTimeouts(std::chrono::milliseconds handshake,
                     std::chrono::milliseconds byte);

private:
    bool syncOld();
    bool syncNew();
    bool waitUntil(const char* what, const std::function<bool()>& predicate,
                   std::chrono::milliseconds timeout);
    void waitReadyForEvenByte();
    void waitReadyForOddByte();

    std::unique_ptr<ParallelPort> port_;
    uint16_t base_ = 0;
    ComMode mode_ = ComMode::Old;
    std::optional<ComMode> preferred_;
    std::chrono::milliseconds handshakeTimeout_ { 4000 };
    std::chrono::milliseconds byteTimeout_ { 1500 };
};

} // namespace tpc2
