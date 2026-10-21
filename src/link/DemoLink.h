// SPDX-License-Identifier: MIT
#pragma once

#include "core/Link.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace tpc2 {

struct DemoFile {
    std::string name;
    std::vector<uint8_t> data;
};

// In-process V64 simulator. It speaks the same GD6 byte stream as the real
// device, which makes it useful for UI demos and for end-to-end tests without
// hardware. It is intentionally not a cycle-accurate model of the 6502 side.
class DemoLink final : public Link {
public:
    explicit DemoLink(bool seedDemoFile = true);

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

    void addDownloadFile(std::string name, std::vector<uint8_t> data);
    void clearFiles() { files_.clear(); }
    [[nodiscard]] const std::vector<DemoFile>& files() const { return files_; }
    [[nodiscard]] uint32_t lastCall() const { return lastCall_; }
    [[nodiscard]] const std::string& lastOsd() const { return lastOsd_; }

private:
    enum class State { Idle, Upload, ExtWrite, Osd };

    void process();
    void buildDownload();
    void handleExpanded(uint8_t code, const uint8_t* args);
    std::pair<uint8_t*, std::size_t> mapMemory(uint32_t address, uint32_t length);

    bool open_ = false;
    ComMode mode_ = ComMode::Old;
    std::optional<ComMode> preferred_;
    State state_ = State::Idle;
    std::vector<uint8_t> in_;
    std::deque<uint8_t> out_;

    std::vector<DemoFile> files_;

    std::string uploadName_;
    uint32_t uploadRemaining_ = 0;
    std::vector<uint8_t> uploadData_;

    uint32_t extAddress_ = 0;
    uint32_t extRemaining_ = 0;

    std::string osdText_;
    std::string lastOsd_;
    uint32_t lastCall_ = 0;

    std::vector<uint8_t> dram_;
    std::vector<uint8_t> cpu_;
};

} // namespace tpc2
