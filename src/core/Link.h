// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace tpc2 {

enum class ComMode : uint8_t {
    Old = 0, // SLCT_IN / AUTO.FX handshake, original BIOS default
    New = 1, // ERROR / SLCT handshake, BIOS 1.2x and later
};

inline const char* comModeName(ComMode mode) noexcept
{
    return mode == ComMode::New ? "new (ERROR/SLCT)" : "old (SLCT_IN/AUTO.FX)";
}

class Link {
public:
    virtual ~Link() = default;

    virtual void open() = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;

    // Runs the three pulse synchronization sequence. Returns the mode that
    // actually worked, which may differ from the preferred mode when auto
    // negotiation was requested.
    virtual ComMode sync() = 0;

    // The V64 raises/lowers BUSY after accepting a command header. This waits
    // for that handshake and drops data bit D7.
    virtual void checkSync() = 0;

    virtual void write(std::span<const uint8_t> data) = 0;
    virtual void read(std::span<uint8_t> out) = 0;

    [[nodiscard]] virtual ComMode mode() const = 0;
    virtual void setPreferredMode(std::optional<ComMode> mode) = 0;
    virtual void reset() = 0;

    [[nodiscard]] virtual std::string description() const = 0;
};

} // namespace tpc2
