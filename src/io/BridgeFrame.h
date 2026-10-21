// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace tpc2::bridge {

// Keep these values in sync with firmware/pico/main.c.
enum class Command : uint8_t {
    Ping = 0x01,
    Sync = 0x02,
    SetMode = 0x03,
    Write = 0x04,
    Read = 0x05,
    CheckSync = 0x06,
    Reset = 0x07,
    Info = 0x08,
};

enum class Status : uint8_t {
    Ok = 0,
    Timeout = 1,
    Protocol = 2,
    Io = 3,
    Unsupported = 4,
    Cancelled = 5,
    BadRequest = 6,
};

struct Frame {
    uint8_t command = 0;
    uint8_t sequence = 0;
    uint8_t status = 0;
    std::vector<uint8_t> data;
    bool response = false;
};

inline constexpr uint8_t kSlipEnd = 0xC0;
inline constexpr uint8_t kSlipEsc = 0xDB;
inline constexpr uint8_t kSlipEscEnd = 0xDC;
inline constexpr uint8_t kSlipEscEsc = 0xDD;
inline constexpr std::size_t kMaxFrame = 65536;

uint16_t crc16(std::span<const uint8_t> data);

std::vector<uint8_t> encode(const Frame& frame);

class Decoder {
public:
    // Returns every complete, CRC-checked frame found in the new bytes.
    std::vector<Frame> feed(std::span<const uint8_t> bytes);

private:
    std::vector<uint8_t> raw_;
};

} // namespace tpc2::bridge
