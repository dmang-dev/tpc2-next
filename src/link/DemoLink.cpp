// SPDX-License-Identifier: MIT
#include "link/DemoLink.h"

#include "core/Error.h"
#include "core/Protocol.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

namespace tpc2 {
namespace {

constexpr uint32_t kDramSize = 4u * 1024u * 1024u;
constexpr uint32_t kCpuBase = 0x8000'0000u;
constexpr uint32_t kCpuSize = 0x0001'0000u;

bool startsWithGd6(const std::vector<uint8_t>& in)
{
    return in.size() >= 4 && in[0] == 'G' && in[1] == 'D' && in[2] == '6';
}

} // namespace

DemoLink::DemoLink(bool seedDemoFile)
    : dram_(kDramSize), cpu_(kCpuSize)
{
    for (uint32_t i = 0; i < kDramSize; ++i)
        dram_[i] = static_cast<uint8_t>((i * 7u + 0x5Au) & 0xFF);
    for (uint32_t i = 0; i < kCpuSize; ++i)
        cpu_[i] = static_cast<uint8_t>(i & 0xFF);

    // A tiny valid-looking N64 header makes the demo download realistic.
    if (seedDemoFile) {
        std::vector<uint8_t> demo(65536, 0);
        const uint8_t header[0x40] = {
            0x80, 0x37, 0x12, 0x40, // PI BSD domain 1
            0x00, 0x00, 0x00, 0x0F, // clock rate
            0x80, 0x00, 0x10, 0x00, // entry point
            0x00, 0x00, 0x00, 0x00,
            0x44, 0x45, 0x4D, 0x4F, 0x20, 0x44, 0x49, 0x53, 0x43, 0x20, 0x20, 0x20,
            0x20, 0x20, 0x20, 0x20, // "DEMO DISC"
        };
        std::memcpy(demo.data(), header, sizeof(header));
        files_.push_back(DemoFile { "DEMO.Z64", std::move(demo) });
    }
}

void DemoLink::open()
{
    open_ = true;
    in_.clear();
    out_.clear();
    state_ = State::Idle;
}

void DemoLink::close()
{
    open_ = false;
    in_.clear();
    out_.clear();
    state_ = State::Idle;
}

bool DemoLink::isOpen() const
{
    return open_;
}

ComMode DemoLink::sync()
{
    if (!open_)
        throw Error(ErrorCode::NotConnected, "Demo device is not open");
    if (preferred_)
        mode_ = *preferred_;
    return mode_;
}

void DemoLink::checkSync()
{
    if (!open_)
        throw Error(ErrorCode::NotConnected, "Demo device is not open");
}

void DemoLink::write(std::span<const uint8_t> data)
{
    if (!open_)
        throw Error(ErrorCode::NotConnected, "Demo device is not open");
    in_.insert(in_.end(), data.begin(), data.end());
    process();
}

void DemoLink::read(std::span<uint8_t> out)
{
    if (!open_)
        throw Error(ErrorCode::NotConnected, "Demo device is not open");
    if (out_.size() < out.size()) {
        throw Error(ErrorCode::Protocol,
                    "Demo device has " + std::to_string(out_.size())
                        + " bytes queued but the host asked for "
                        + std::to_string(out.size()));
    }
    for (uint8_t& byte : out) {
        byte = out_.front();
        out_.pop_front();
    }
}

void DemoLink::reset()
{
    in_.clear();
    out_.clear();
    state_ = State::Idle;
    uploadData_.clear();
    uploadRemaining_ = 0;
}

std::string DemoLink::description() const
{
    return "Demo device (simulated Doctor V64)";
}

void DemoLink::addDownloadFile(std::string name, std::vector<uint8_t> data)
{
    files_.push_back(DemoFile { std::move(name), std::move(data) });
}

void DemoLink::buildDownload()
{
    out_.clear();
    if (files_.size() > 255)
        throw Error(ErrorCode::Protocol, "Demo device has too many files");
    out_.push_back(static_cast<uint8_t>(files_.size()));
    for (const auto& file : files_) {
        const auto encoded = proto::encodeName(file.name);
        std::vector<uint8_t> header;
        header.reserve(15);
        proto::appendLe32(header, static_cast<uint32_t>(file.data.size()));
        header.insert(header.end(), encoded.begin(), encoded.end());
        out_.insert(out_.end(), header.begin(), header.end());
        out_.insert(out_.end(), file.data.begin(), file.data.end());
    }
}

std::pair<uint8_t*, std::size_t> DemoLink::mapMemory(uint32_t address,
                                                     uint32_t length)
{
    if (address >= kCpuBase && static_cast<uint64_t>(address) + length
            <= static_cast<uint64_t>(kCpuBase) + kCpuSize) {
        return { cpu_.data() + (address - kCpuBase), kCpuSize };
    }
    if (static_cast<uint64_t>(address) + length <= kDramSize)
        return { dram_.data() + address, kDramSize };
    throw Error(ErrorCode::InvalidArgument,
                "Demo device memory range is outside DRAM/CPU space");
}

void DemoLink::handleExpanded(uint8_t code, const uint8_t* args)
{
    switch (code) {
    case 'R': {
        const uint32_t address = proto::readBe32(args);
        const uint32_t length = proto::readBe32(args + 4);
        if (length == 0)
            throw Error(ErrorCode::InvalidArgument, "Read length is zero");
        auto [ptr, available] = mapMemory(address, length);
        (void)available;
        out_.push_back(0xA0);
        out_.insert(out_.end(), ptr, ptr + length);
        break;
    }
    case 'W': {
        const uint32_t address = proto::readBe32(args);
        const uint32_t length = proto::readBe32(args + 4);
        if (length == 0)
            throw Error(ErrorCode::InvalidArgument, "Write length is zero");
        auto [ptr, available] = mapMemory(address, length);
        (void)ptr;
        (void)available;
        extAddress_ = address;
        extRemaining_ = length;
        state_ = State::ExtWrite;
        out_.push_back(0xA1);
        break;
    }
    case 'C': {
        const uint32_t address = static_cast<uint32_t>(args[0] << 8 | args[1]);
        if (address > 0xFFFF)
            throw Error(ErrorCode::InvalidArgument, "Call address is out of range");
        lastCall_ = address;
        out_.push_back(0xA2);
        break;
    }
    case 'P': {
        if (args[0] > 0x0A || args[1] > 0x17)
            throw Error(ErrorCode::InvalidArgument, "OSD position is out of range");
        osdText_.clear();
        state_ = State::Osd;
        out_.push_back(0xA3);
        break;
    }
    default:
        throw Error(ErrorCode::Protocol, "Unknown expanded command in demo device");
    }
}

void DemoLink::process()
{
    while (true) {
        if (state_ == State::Upload) {
            const std::size_t count =
                std::min<std::size_t>(in_.size(), uploadRemaining_);
            uploadData_.insert(uploadData_.end(), in_.begin(),
                               in_.begin() + static_cast<std::ptrdiff_t>(count));
            in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(count));
            uploadRemaining_ -= static_cast<uint32_t>(count);
            if (uploadRemaining_ > 0)
                return;
            files_.push_back(DemoFile { uploadName_, std::move(uploadData_) });
            uploadData_.clear();
            state_ = State::Idle;
            continue;
        }

        if (state_ == State::ExtWrite) {
            const std::size_t count =
                std::min<std::size_t>(in_.size(), extRemaining_);
            auto [ptr, available] = mapMemory(extAddress_, extRemaining_);
            (void)available;
            std::copy(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(count), ptr);
            in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(count));
            extAddress_ += static_cast<uint32_t>(count);
            extRemaining_ -= static_cast<uint32_t>(count);
            if (extRemaining_ > 0)
                return;
            state_ = State::Idle;
            continue;
        }

        if (state_ == State::Osd) {
            const auto end = std::find(in_.begin(), in_.end(), 0xFF);
            if (end == in_.end()) {
                osdText_.append(reinterpret_cast<const char*>(in_.data()), in_.size());
                in_.clear();
                return;
            }
            osdText_.append(reinterpret_cast<const char*>(in_.data()),
                            static_cast<std::size_t>(end - in_.begin()));
            lastOsd_ = osdText_;
            osdText_.clear();
            in_.erase(in_.begin(), end + 1);
            state_ = State::Idle;
            continue;
        }

        // Idle: parse a command header.
        if (in_.size() < 4)
            return;
        if (!startsWithGd6(in_)) {
            in_.clear();
            throw Error(ErrorCode::Protocol, "Demo device expected a GD6 header");
        }
        if (in_.size() < 4)
            return;

        const uint8_t command = in_[3];
        if (command == 'R') {
            if (in_.size() < 20)
                return;
            const uint32_t length = proto::readLe32(in_.data() + 5);
            uploadName_ = proto::decodeName(in_.data() + 9);
            in_.erase(in_.begin(), in_.begin() + 20);
            uploadData_.clear();
            uploadRemaining_ = length;
            state_ = State::Upload;
            continue;
        }

        if (command == 'W') {
            // GD6W is followed by the original TPC2's 11-byte 8.3 name field.
            if (in_.size() < 15)
                return;
            in_.erase(in_.begin(), in_.begin() + 15);
            buildDownload();
            continue;
        }

        if (command == 'E') {
            if (in_.size() < 5)
                return;
            const uint8_t code = in_[4];
            const std::size_t argCount = (code == 'R' || code == 'W') ? 8 : 2;
            const std::size_t need = 5 + argCount;
            if (in_.size() < need)
                return;
            std::array<uint8_t, 8> args {};
            std::copy(in_.begin() + 5, in_.begin() + 5 + static_cast<std::ptrdiff_t>(argCount),
                      args.begin());
            handleExpanded(code, args.data());
            in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(need));
            continue;
        }

        in_.clear();
        throw Error(ErrorCode::Protocol,
                    "Unknown GD6 command in demo device: 0x"
                        + [&] {
                              char buf[8];
                              std::snprintf(buf, sizeof(buf), "%02X", command);
                              return std::string(buf);
                          }()
                        + " (buffer " + std::to_string(in_.size()) + ")");
    }
}

} // namespace tpc2
