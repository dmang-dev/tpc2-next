// SPDX-License-Identifier: MIT
#pragma once

#include "platform/ParallelPort.h"
#include "v64_model.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace tpc2::test {

struct V64Event {
    v64_event_kind kind;
    uint32_t a = 0;
    uint32_t b = 0;
    std::vector<uint8_t> bytes;
};

struct TraceEntry {
    uint64_t time = 0;
    char who = 'H'; // 'H' = PC changed a pin, 'V' = V64 output became visible
    v64_host_pins host {};
    v64_dev_pins dev {};
};

// Owns a V64 model, its memory, the simulation bus and the recorded events.
// Not copyable: the model's callbacks point back at this object.
class V64Sim {
public:
    explicit V64Sim(uint32_t dramSize = 1u << 20);
    V64Sim(const V64Sim&) = delete;
    V64Sim& operator=(const V64Sim&) = delete;

    // Live configuration; the model reads it on every pin change.
    [[nodiscard]] v64_config& config() { return model_.cfg; }

    // Front-panel reset: clears protocol state, keeps configuration.
    void reset();
    void setDelay(uint32_t microseconds) { bus_.delay_us = microseconds; }

    void addFile(const std::string& name, std::vector<uint8_t> data);
    void addDramFile(const std::string& name, uint32_t offset, uint32_t size);
    void clearFiles();

    [[nodiscard]] std::vector<uint8_t>& dram() { return dram_; }
    [[nodiscard]] std::vector<uint8_t>& cpu() { return cpu_; }

    [[nodiscard]] const std::vector<V64Event>& events() const { return events_; }
    [[nodiscard]] std::vector<V64Event> eventsOf(v64_event_kind kind) const;
    void clearEvents() { events_.clear(); }

    void setTracing(bool enabled) { tracing_ = enabled; }
    [[nodiscard]] const std::vector<TraceEntry>& trace() const { return trace_; }
    void clearTrace() { trace_.clear(); }
    // One line per pin change, without timestamps, so traces from different
    // hosts can be diffed. Consecutive identical states are merged.
    [[nodiscard]] std::vector<std::string> traceLines(bool withTime = false) const;

    void setHost(const v64_host_pins& pins) { v64_bus_set_host(&bus_, &pins); }
    void tick(uint32_t microseconds) { v64_bus_tick(&bus_, microseconds); }
    [[nodiscard]] const v64_host_pins& host() const { return bus_.host; }
    [[nodiscard]] const v64_dev_pins& visible() const { return bus_.visible; }
    [[nodiscard]] uint64_t nowUs() const { return bus_.now_us; }
    [[nodiscard]] std::chrono::steady_clock::time_point now() const;
    [[nodiscard]] v64_bus& bus() { return bus_; }

private:
    static void onEvent(void* user, const v64_event* event);
    static void onTrace(void* user, uint64_t time, char who,
                        const v64_host_pins* host, const v64_dev_pins* dev);

    std::vector<uint8_t> dram_;
    std::vector<uint8_t> cpu_;
    std::deque<std::vector<uint8_t>> fileData_;
    v64_model model_ {};
    v64_bus bus_ {};
    std::vector<V64Event> events_;
    std::vector<TraceEntry> trace_;
    bool tracing_ = false;
};

// ParallelPort that plays the PC side of a standard SPP port wired to the
// simulated V64. Register polarity follows a real PC: in the status register
// only BUSY (bit 7) is inverted; in the control register STROBE, AUTOFD and
// SELECTIN are inverted and INIT is not. Every register access advances the
// simulated clock by 1 us, roughly the cost of an ISA port access.
class FakeV64Port final : public ParallelPort {
public:
    explicit FakeV64Port(V64Sim& sim) : sim_(sim) {}

    void open(uint16_t base) override;
    void close() override { open_ = false; }
    [[nodiscard]] bool isOpen() const override { return open_; }

    void writeData(uint8_t value) override;
    [[nodiscard]] uint8_t readStatus() override;
    [[nodiscard]] uint8_t readControl() override;
    void writeControl(uint8_t value) override;

    [[nodiscard]] std::string description() const override;

    // Unused register bits read as ones on many chipsets. Returning them
    // checks that every host masks the bits it uses.
    static constexpr uint8_t kStatusReserved = 0x07;
    static constexpr uint8_t kControlReserved = 0xC0;

private:
    V64Sim& sim_;
    bool open_ = false;
    uint16_t base_ = 0;
};

// Encodes a "NAME.EXT" string to the raw 11-byte 8.3 field, without the
// sanitising done by proto::encodeName, so tests can send unusual names.
std::array<uint8_t, 11> rawName(const std::string& name);

} // namespace tpc2::test
