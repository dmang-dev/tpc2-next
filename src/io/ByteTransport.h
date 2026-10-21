// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace tpc2 {

// Byte oriented transport used by the USB bridge. Implementations own the OS
// handle (serial port, socket, test pipe, ...).
class ByteTransport {
public:
    virtual ~ByteTransport() = default;
    virtual void open() = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;
    virtual std::size_t read(uint8_t* out, std::size_t maxBytes, int timeoutMs) = 0;
    virtual void write(const uint8_t* data, std::size_t size) = 0;
    [[nodiscard]] virtual std::string description() const = 0;
};

} // namespace tpc2
