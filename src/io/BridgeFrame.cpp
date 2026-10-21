// SPDX-License-Identifier: MIT
#include "io/BridgeFrame.h"

#include "core/Error.h"

namespace tpc2::bridge {

uint16_t crc16(std::span<const uint8_t> data)
{
    uint16_t crc = 0xFFFF;
    for (uint8_t byte : data) {
        crc ^= static_cast<uint16_t>(byte) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

std::vector<uint8_t> encode(const Frame& frame)
{
    if (frame.data.size() > kMaxFrame)
        throw Error(ErrorCode::InvalidArgument, "Bridge frame is too large");

    std::vector<uint8_t> body;
    body.reserve(frame.data.size() + 7);
    body.push_back(static_cast<uint8_t>(frame.command | (frame.response ? 0x80 : 0x00)));
    body.push_back(frame.sequence);
    body.push_back(frame.status);
    body.push_back(static_cast<uint8_t>(frame.data.size()));
    body.push_back(static_cast<uint8_t>(frame.data.size() >> 8));
    body.insert(body.end(), frame.data.begin(), frame.data.end());
    const uint16_t crc = crc16(body);
    body.push_back(static_cast<uint8_t>(crc));
    body.push_back(static_cast<uint8_t>(crc >> 8));

    std::vector<uint8_t> out;
    out.reserve(body.size() + 2);
    for (uint8_t byte : body) {
        if (byte == kSlipEnd) {
            out.push_back(kSlipEsc);
            out.push_back(kSlipEscEnd);
        } else if (byte == kSlipEsc) {
            out.push_back(kSlipEsc);
            out.push_back(kSlipEscEsc);
        } else {
            out.push_back(byte);
        }
    }
    out.push_back(kSlipEnd);
    return out;
}

std::vector<Frame> Decoder::feed(std::span<const uint8_t> bytes)
{
    std::vector<Frame> frames;
    for (uint8_t byte : bytes) {
        if (byte == kSlipEnd) {
            if (raw_.empty())
                continue;
            const std::vector<uint8_t> raw = std::move(raw_);
            raw_.clear();

            std::vector<uint8_t> body;
            body.reserve(raw.size());
            bool escape = false;
            for (uint8_t b : raw) {
                if (escape) {
                    if (b == kSlipEscEnd)
                        body.push_back(kSlipEnd);
                    else if (b == kSlipEscEsc)
                        body.push_back(kSlipEsc);
                    else
                        throw Error(ErrorCode::Protocol, "Invalid SLIP escape");
                    escape = false;
                } else if (b == kSlipEsc) {
                    escape = true;
                } else {
                    body.push_back(b);
                }
            }
            if (escape)
                throw Error(ErrorCode::Protocol, "Truncated SLIP escape");
            if (body.size() < 7)
                continue;

            const uint16_t claimed = static_cast<uint16_t>(body[body.size() - 2])
                | (static_cast<uint16_t>(body[body.size() - 1]) << 8);
            const uint16_t actual = crc16(
                std::span<const uint8_t>(body.data(), body.size() - 2));
            if (claimed != actual)
                throw Error(ErrorCode::Checksum, "Bridge frame CRC mismatch");

            const std::size_t length = static_cast<std::size_t>(body[3])
                | (static_cast<std::size_t>(body[4]) << 8);
            if (body.size() != length + 7)
                throw Error(ErrorCode::Protocol, "Bridge frame length mismatch");

            Frame frame;
            frame.command = static_cast<uint8_t>(body[0] & 0x7F);
            frame.response = (body[0] & 0x80) != 0;
            frame.sequence = body[1];
            frame.status = body[2];
            frame.data.assign(body.begin() + 5, body.begin() + 5
                                                   + static_cast<std::ptrdiff_t>(length));
            frames.push_back(std::move(frame));
            continue;
        }

        raw_.push_back(byte);
        if (raw_.size() > kMaxFrame + 8) {
            raw_.clear();
            throw Error(ErrorCode::Protocol, "Bridge frame exceeded the size limit");
        }
    }
    return frames;
}

} // namespace tpc2::bridge
