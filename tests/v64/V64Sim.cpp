// SPDX-License-Identifier: MIT
#include "V64Sim.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace tpc2::test {

std::array<uint8_t, 11> rawName(const std::string& name)
{
    std::array<uint8_t, 11> out {};
    out.fill(' ');
    const auto dot = name.find('.');
    const std::string stem = name.substr(0, dot);
    const std::string ext = dot == std::string::npos ? "" : name.substr(dot + 1);
    std::copy_n(stem.begin(), std::min<std::size_t>(stem.size(), 8), out.begin());
    std::copy_n(ext.begin(), std::min<std::size_t>(ext.size(), 3), out.begin() + 8);
    return out;
}

V64Sim::V64Sim(uint32_t dramSize)
    : dram_(dramSize), cpu_(0x10000)
{
    for (std::size_t i = 0; i < dram_.size(); ++i)
        dram_[i] = static_cast<uint8_t>((i * 13u + 0x21u) & 0xFF);
    for (std::size_t i = 0; i < cpu_.size(); ++i)
        cpu_[i] = static_cast<uint8_t>((i >> 8) ^ i);

    v64_config cfg;
    v64_config_defaults(&cfg);
    cfg.dram = dram_.data();
    cfg.dram_size = static_cast<uint32_t>(dram_.size());
    cfg.cpu = cpu_.data();
    cfg.on_event = &V64Sim::onEvent;
    cfg.user = this;
    v64_init(&model_, &cfg);
    v64_bus_init(&bus_, &model_);
    bus_.trace = &V64Sim::onTrace;
    bus_.trace_user = this;
}

void V64Sim::reset()
{
    v64_reset(&model_);
    model_.prev = bus_.host; // no spurious edges from the reset itself
    v64_update(&model_, &bus_.host);
    bus_.pending_count = 0;
    bus_.visible = model_.out;
}

void V64Sim::addFile(const std::string& name, std::vector<uint8_t> data)
{
    auto& cfg = model_.cfg;
    if (cfg.file_count >= V64_MAX_FILES)
        return;
    fileData_.push_back(std::move(data));
    v64_file& file = cfg.files[cfg.file_count++];
    const auto field = rawName(name);
    std::memcpy(file.name, field.data(), field.size());
    file.data = fileData_.back().data();
    file.dram_offset = 0;
    file.size = static_cast<uint32_t>(fileData_.back().size());
}

void V64Sim::addDramFile(const std::string& name, uint32_t offset, uint32_t size)
{
    auto& cfg = model_.cfg;
    if (cfg.file_count >= V64_MAX_FILES)
        return;
    v64_file& file = cfg.files[cfg.file_count++];
    const auto field = rawName(name);
    std::memcpy(file.name, field.data(), field.size());
    file.data = nullptr;
    file.dram_offset = offset;
    file.size = size;
}

void V64Sim::clearFiles()
{
    model_.cfg.file_count = 0;
    fileData_.clear();
}

std::vector<V64Event> V64Sim::eventsOf(v64_event_kind kind) const
{
    std::vector<V64Event> out;
    for (const auto& event : events_) {
        if (event.kind == kind)
            out.push_back(event);
    }
    return out;
}

std::chrono::steady_clock::time_point V64Sim::now() const
{
    return std::chrono::steady_clock::time_point(
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::microseconds(bus_.now_us)));
}

void V64Sim::onEvent(void* user, const v64_event* event)
{
    auto* self = static_cast<V64Sim*>(user);
    V64Event copy { event->kind, event->a, event->b, {} };
    if (event->bytes && event->length)
        copy.bytes.assign(event->bytes, event->bytes + event->length);
    self->events_.push_back(std::move(copy));
}

void V64Sim::onTrace(void* user, uint64_t time, char who,
                     const v64_host_pins* host, const v64_dev_pins* dev)
{
    auto* self = static_cast<V64Sim*>(user);
    if (self->tracing_)
        self->trace_.push_back(TraceEntry { time, who, *host, *dev });
}

std::vector<std::string> V64Sim::traceLines(bool withTime) const
{
    std::vector<std::string> lines;
    std::string last;
    for (const auto& entry : trace_) {
        const auto& h = entry.host;
        const auto& d = entry.dev;
        // D=data STB/INI = PC pins, AF/SI = open-drain line levels,
        // then BUSY nACK PE SLCT nERROR as driven by the V64.
        char buf[96];
        std::snprintf(buf, sizeof buf,
                      "D=%02X STB=%u INI=%u AF=%u SI=%u | BSY=%u ACK=%u PE=%u SL=%u ER=%u",
                      h.data, h.nstrobe, h.ninit, v64_line_autofd(&h, &d),
                      v64_line_selectin(&h, &d), d.busy, d.nack, d.pe, d.slct,
                      d.nerror);
        std::string line = buf;
        if (line == last)
            continue;
        last = line;
        if (withTime) {
            char stamp[32];
            std::snprintf(stamp, sizeof stamp, "%10llu %c ",
                          static_cast<unsigned long long>(entry.time), entry.who);
            line = stamp + line;
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

void FakeV64Port::open(uint16_t base)
{
    base_ = base;
    open_ = true;
}

void FakeV64Port::writeData(uint8_t value)
{
    v64_host_pins pins = sim_.host();
    pins.data = value;
    sim_.setHost(pins);
    sim_.tick(1);
}

void FakeV64Port::writeControl(uint8_t value)
{
    v64_host_pins pins = sim_.host();
    pins.nstrobe = (value & 0x01) ? 0 : 1;      // C0 inverted
    pins.autofd_low = (value & 0x02) ? 1 : 0;   // C1 inverted, open drain
    pins.ninit = (value & 0x04) ? 1 : 0;        // C2 not inverted
    pins.selectin_low = (value & 0x08) ? 1 : 0; // C3 inverted, open drain
    sim_.setHost(pins);
    sim_.tick(1);
}

uint8_t FakeV64Port::readStatus()
{
    sim_.tick(1);
    const v64_dev_pins& d = sim_.visible();
    uint8_t value = kStatusReserved;
    if (!d.busy)
        value |= 0x80; // S7 = inverted BUSY
    if (d.nack)
        value |= 0x40; // S6 = nACK, not inverted
    if (d.pe)
        value |= 0x20;
    if (d.slct)
        value |= 0x10;
    if (d.nerror)
        value |= 0x08; // S3 = nERROR, not inverted
    return value;
}

uint8_t FakeV64Port::readControl()
{
    sim_.tick(1);
    const v64_host_pins& h = sim_.host();
    const v64_dev_pins& d = sim_.visible();
    uint8_t value = kControlReserved;
    if (!h.nstrobe)
        value |= 0x01;
    if (!v64_line_autofd(&h, &d))
        value |= 0x02; // reads the wired-AND line, inverted
    if (h.ninit)
        value |= 0x04;
    if (!v64_line_selectin(&h, &d))
        value |= 0x08;
    return value;
}

std::string FakeV64Port::description() const
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "simulated V64 at 0x%03X", base_);
    return buf;
}

} // namespace tpc2::test
