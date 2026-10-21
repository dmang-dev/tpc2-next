// SPDX-License-Identifier: MIT
#include "link/BridgeLink.h"

#include "core/Error.h"

#include <algorithm>
#include <array>
#include <thread>

namespace tpc2 {
namespace {

ErrorCode mapStatus(bridge::Status status)
{
    switch (status) {
    case bridge::Status::Ok: return ErrorCode::Ok;
    case bridge::Status::Timeout: return ErrorCode::Timeout;
    case bridge::Status::Protocol: return ErrorCode::Protocol;
    case bridge::Status::Io: return ErrorCode::Io;
    case bridge::Status::Unsupported: return ErrorCode::Unsupported;
    case bridge::Status::Cancelled: return ErrorCode::Cancelled;
    case bridge::Status::BadRequest: return ErrorCode::InvalidArgument;
    }
    return ErrorCode::Protocol;
}

std::string payloadText(const std::vector<uint8_t>& data)
{
    if (data.empty())
        return {};
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

} // namespace

BridgeLink::BridgeLink(std::unique_ptr<ByteTransport> transport)
    : transport_(std::move(transport))
{
}

void BridgeLink::open()
{
    if (!transport_)
        throw Error(ErrorCode::Unsupported, "No serial transport configured");
    transport_->open();
    open_ = true;
    try {
        const auto response = transact(bridge::Command::Ping, {}, transactionTimeout_);
        firmwareVersion_ = payloadText(response.data);
        if (firmwareVersion_.empty())
            firmwareVersion_ = "unknown";
        if (preferred_)
            setPreferredMode(preferred_);
    } catch (...) {
        close();
        throw;
    }
}

void BridgeLink::close()
{
    open_ = false;
    if (transport_)
        transport_->close();
}

bool BridgeLink::isOpen() const
{
    return open_ && transport_ && transport_->isOpen();
}

std::optional<bridge::Frame> BridgeLink::readFrame(std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::array<uint8_t, 512> buffer {};
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        const int waitMs = static_cast<int>(
            std::max<int64_t>(1, std::min<int64_t>(remaining.count(), 200)));
        const std::size_t got = transport_->read(buffer.data(), buffer.size(), waitMs);
        if (got == 0)
            continue;
        auto frames = decoder_.feed(
            std::span<const uint8_t>(buffer.data(), got));
        for (auto& frame : frames) {
            if (frame.response)
                return frame;
        }
    }
    return std::nullopt;
}

bridge::Frame BridgeLink::transact(bridge::Command command,
                                   const std::vector<uint8_t>& payload,
                                   std::chrono::milliseconds timeout)
{
    if (!transport_ || !transport_->isOpen())
        throw Error(ErrorCode::NotConnected, "USB bridge is not connected");

    bridge::Frame request;
    request.command = static_cast<uint8_t>(command);
    request.sequence = sequence_++;
    request.data = payload;

    const auto encoded = bridge::encode(request);
    transport_->write(encoded.data(), encoded.size());

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        auto frame = readFrame(remaining);
        if (!frame)
            break;
        if (frame->sequence != request.sequence)
            continue; // stale response from an earlier cancelled transaction
        if (frame->command != static_cast<uint8_t>(
                static_cast<uint8_t>(command) | 0x80))
            continue;
        raiseForStatus(*frame);
        return *frame;
    }
    throw Error(ErrorCode::Timeout,
                "No response from the USB bridge (is the firmware running?)");
}

void BridgeLink::raiseForStatus(const bridge::Frame& frame)
{
    if (frame.status == static_cast<uint8_t>(bridge::Status::Ok))
        return;
    const auto code = mapStatus(static_cast<bridge::Status>(frame.status));
    std::string message = payloadText(frame.data);
    if (message.empty())
        message = "USB bridge reported an error";
    throw Error(code, message);
}

ComMode BridgeLink::sync()
{
    uint8_t preferredValue = 0;
    if (preferred_)
        preferredValue = *preferred_ == ComMode::Old ? 1 : 2;

    const auto response = transact(bridge::Command::Sync,
                                   std::vector<uint8_t> { preferredValue },
                                   transactionTimeout_);
    if (response.data.empty())
        throw Error(ErrorCode::Protocol, "Bridge sync response has no mode byte");
    mode_ = response.data[0] == 1 ? ComMode::New : ComMode::Old;
    return mode_;
}

void BridgeLink::checkSync()
{
    transact(bridge::Command::CheckSync, {}, transactionTimeout_);
}

void BridgeLink::write(std::span<const uint8_t> data)
{
    constexpr std::size_t kBridgeChunk = 8192;
    std::size_t offset = 0;
    while (offset < data.size()) {
        const std::size_t count = std::min(kBridgeChunk, data.size() - offset);
        const std::vector<uint8_t> chunk(data.begin() + static_cast<std::ptrdiff_t>(offset),
                                         data.begin() + static_cast<std::ptrdiff_t>(offset + count));
        transact(bridge::Command::Write, chunk, blockTimeout_);
        offset += count;
    }
}

void BridgeLink::read(std::span<uint8_t> out)
{
    constexpr std::size_t kBridgeChunk = 8192;
    std::size_t offset = 0;
    while (offset < out.size()) {
        const std::size_t count = std::min(kBridgeChunk, out.size() - offset);
        std::vector<uint8_t> request;
        request.push_back(static_cast<uint8_t>(count));
        request.push_back(static_cast<uint8_t>(count >> 8));
        request.push_back(static_cast<uint8_t>(count >> 16));
        request.push_back(static_cast<uint8_t>(count >> 24));

        const auto response = transact(bridge::Command::Read, request, blockTimeout_);
        if (response.data.size() != count)
            throw Error(ErrorCode::Protocol,
                        "Bridge returned " + std::to_string(response.data.size())
                            + " bytes, expected " + std::to_string(count));
        std::copy(response.data.begin(), response.data.end(),
                  out.begin() + static_cast<std::ptrdiff_t>(offset));
        offset += count;
    }
}

void BridgeLink::setPreferredMode(std::optional<ComMode> mode)
{
    preferred_ = mode;
    if (!isOpen())
        return;
    uint8_t value = 0;
    if (mode)
        value = *mode == ComMode::Old ? 1 : 2;
    transact(bridge::Command::SetMode, std::vector<uint8_t> { value },
             transactionTimeout_);
}

void BridgeLink::reset()
{
    if (isOpen())
        transact(bridge::Command::Reset, {}, transactionTimeout_);
}

std::string BridgeLink::description() const
{
    return "USB bridge (" + (transport_ ? transport_->description() : "serial")
        + ", fw " + firmwareVersion_ + ")";
}

} // namespace tpc2
