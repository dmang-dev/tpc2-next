// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tpc2 {

struct PortDescriptor {
    std::string name;
    uint16_t base = 0;
    bool available = false;
    std::string detail;
};

// Thin SPP register access. Data/status/control correspond to base+0, +1, +2.
class ParallelPort {
public:
    virtual ~ParallelPort() = default;

    virtual void open(uint16_t base) = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;

    virtual void writeData(uint8_t value) = 0;
    [[nodiscard]] virtual uint8_t readStatus() = 0;
    [[nodiscard]] virtual uint8_t readControl() = 0;
    virtual void writeControl(uint8_t value) = 0;

    [[nodiscard]] virtual std::string description() const = 0;

    static std::vector<PortDescriptor> enumerate();
    static std::unique_ptr<ParallelPort> create();
    static bool supported();
};

} // namespace tpc2
