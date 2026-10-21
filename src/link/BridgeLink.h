// SPDX-License-Identifier: MIT
#pragma once

#include "core/Link.h"
#include "io/BridgeFrame.h"
#include "io/ByteTransport.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace tpc2 {

// Host side of the USB/serial bridge protocol. The microcontroller performs
// the time-critical parallel-port handshake, so USB latency does not limit the
// transfer rate and no kernel driver or administrator rights are needed.
class BridgeLink final : public Link {
public:
    explicit BridgeLink(std::unique_ptr<ByteTransport> transport);

    void open() override;
    void close() override;
    [[nodiscard]] bool isOpen() const override;

    ComMode sync() override;
    void checkSync() override;
    void write(std::span<const uint8_t> data) override;
    void read(std::span<uint8_t> out) override;

    [[nodiscard]] ComMode mode() const override { return mode_; }
    void setPreferredMode(std::optional<ComMode> mode) override;
    void reset() override;

    [[nodiscard]] std::string description() const override;

    void setTimeouts(std::chrono::milliseconds transaction,
                     std::chrono::milliseconds block);

private:
    bridge::Frame transact(bridge::Command command,
                           const std::vector<uint8_t>& payload,
                           std::chrono::milliseconds timeout);
    void raiseForStatus(const bridge::Frame& frame);
    std::optional<bridge::Frame> readFrame(std::chrono::milliseconds timeout);

    std::unique_ptr<ByteTransport> transport_;
    bridge::Decoder decoder_;
    uint8_t sequence_ = 1;
    ComMode mode_ = ComMode::Old;
    std::optional<ComMode> preferred_;
    std::string firmwareVersion_ = "unknown";
    std::chrono::milliseconds transactionTimeout_ { 8000 };
    std::chrono::milliseconds blockTimeout_ { 20000 };
    bool open_ = false;
};

} // namespace tpc2
