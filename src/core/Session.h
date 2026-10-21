// SPDX-License-Identifier: MIT
#pragma once

#include "core/Link.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace tpc2 {

struct ProgressReport {
    uint64_t done = 0;
    uint64_t total = 0; // 0 means "unknown / indeterminate"
};

using ProgressFn = std::function<void(const ProgressReport&)>;
using CancelFn = std::function<bool()>;

struct UploadItem {
    std::filesystem::path path;
    std::string remoteName; // may be empty, then the local name is encoded
};

struct ReceivedFile {
    std::string name;
    std::filesystem::path path;
    uint64_t size = 0;
};

struct DeviceInfo {
    std::string description;
    ComMode mode = ComMode::Old;
};

class Session {
public:
    explicit Session(Link& link);

    // Opens the link if needed and negotiates the parallel handshake.
    DeviceInfo connect();

    // TPC2 "out file": one GD6R command per file. The device can only accept
    // one file at a time, so files are sent sequentially.
    std::vector<std::string> upload(const std::vector<UploadItem>& items,
                                    const ProgressFn& progress = {},
                                    const CancelFn& cancelled = {});

    // TPC2 "in /name": the V64 dumps its current DRAM image. Headers arrive
    // interleaved with data, which is why the total size is not known up
    // front and progress is reported as indeterminate.
    std::vector<ReceivedFile> download(const std::filesystem::path& destination,
                                       const ProgressFn& progress = {},
                                       const CancelFn& cancelled = {});

    // V64IO2 compatible extended commands (BIOS 1.30+).
    std::vector<uint8_t> readMemory(uint32_t address, uint32_t length,
                                    const ProgressFn& progress = {},
                                    const CancelFn& cancelled = {});
    void writeMemory(uint32_t address, std::span<const uint8_t> data,
                     const ProgressFn& progress = {},
                     const CancelFn& cancelled = {});
    void call(uint32_t address);
    // flash sets D7 of every character; necFont sets D6 and selects the
    // NEC6450 on-screen font, as documented by V64IO2.
    void printOsd(uint8_t row, uint8_t column, const std::string& text,
                  bool flash = false, bool necFont = false);

    [[nodiscard]] Link& link() noexcept { return link_; }

private:
    void sendExpandHeader(uint8_t code);
    void sendExpandArgs(uint32_t address, uint32_t length);
    uint8_t expectAck(uint8_t expected, const char* operation);
    static void checkCancelled(const CancelFn& cancelled);

    Link& link_;
};

} // namespace tpc2
