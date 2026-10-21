// SPDX-License-Identifier: MIT
#include "link/ParallelLink.h"

#include "core/Error.h"

#include <array>
#include <cstdio>
#include <functional>
#include <thread>

namespace tpc2 {
namespace {

constexpr uint8_t kStb = 0x01;    // control D0, active low on the wire
constexpr uint8_t kAutoLf = 0x02; // control D1
constexpr uint8_t kInit = 0x04;   // control D2
constexpr uint8_t kSlctIn = 0x08; // control D3
constexpr uint8_t kBusy = 0x80;   // status D7
constexpr uint8_t kSlct = 0x10;   // status D4
constexpr uint8_t kError = 0x08;  // status D3

constexpr uint8_t kReadSelect = 0x80;
constexpr uint8_t kWriteSelect = 0x00;

} // namespace

ParallelLink::ParallelLink(std::unique_ptr<ParallelPort> port,
                           uint16_t baseAddress)
    : port_(std::move(port)), base_(baseAddress)
{
}

void ParallelLink::open()
{
    if (!port_)
        throw Error(ErrorCode::Unsupported, "No parallel port implementation");
    if (!port_->isOpen())
        port_->open(base_);
}

void ParallelLink::close()
{
    if (port_)
        port_->close();
}

bool ParallelLink::isOpen() const
{
    return port_ && port_->isOpen();
}

void ParallelLink::reset()
{
    if (port_ && port_->isOpen()) {
        port_->writeControl(kInit);
        port_->writeData(0);
    }
}

std::string ParallelLink::description() const
{
    return port_ ? port_->description() : "parallel port";
}

void ParallelLink::setTimeouts(std::chrono::milliseconds handshake,
                               std::chrono::milliseconds byte)
{
    handshakeTimeout_ = handshake;
    byteTimeout_ = byte;
}

bool ParallelLink::waitUntil(const char* what,
                             const std::function<bool()>& predicate,
                             std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    unsigned spins = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;
        if ((++spins & 0x3F) == 0)
            std::this_thread::yield();
    }
    (void)what;
    return false;
}

bool ParallelLink::syncOld()
{
    // Pulse 0xAA and 0x55. The V64 acknowledges by pulling SLCT_IN (control
    // D3) low. This is the sequence used by BIOS versions before 1.2x.
    if (!waitUntil("SLCT_IN idle", [&] { return (port_->readControl() & kSlctIn) == 0; },
                   handshakeTimeout_))
        return false;

    port_->writeData(0xAA);
    port_->writeControl(0x00);
    if (!waitUntil("SLCT_IN ack 1", [&] { return (port_->readControl() & kSlctIn) != 0; },
                   handshakeTimeout_))
        return false;
    port_->writeControl(kInit);
    if (!waitUntil("SLCT_IN release 1", [&] { return (port_->readControl() & kSlctIn) == 0; },
                   handshakeTimeout_))
        return false;

    port_->writeData(0x55);
    port_->writeControl(0x00);
    if (!waitUntil("SLCT_IN ack 2", [&] { return (port_->readControl() & kSlctIn) != 0; },
                   handshakeTimeout_))
        return false;
    port_->writeControl(kInit);
    if (!waitUntil("SLCT_IN release 2", [&] { return (port_->readControl() & kSlctIn) == 0; },
                   handshakeTimeout_))
        return false;

    return true;
}

bool ParallelLink::syncNew()
{
    // BIOS 1.2x and later use ERROR (status D3) instead, which is much easier
    // to read on modern Super I/O chips.
    if (!waitUntil("ERROR idle", [&] { return (port_->readStatus() & kError) != 0; },
                   handshakeTimeout_))
        return false;

    port_->writeData(0xA5);
    port_->writeControl(0x00);
    if (!waitUntil("ERROR ack 1", [&] { return (port_->readStatus() & kError) == 0; },
                   handshakeTimeout_))
        return false;
    port_->writeControl(kInit);
    if (!waitUntil("ERROR release 1", [&] { return (port_->readStatus() & kError) != 0; },
                   handshakeTimeout_))
        return false;

    port_->writeData(0x5A);
    port_->writeControl(0x00);
    if (!waitUntil("ERROR ack 2", [&] { return (port_->readStatus() & kError) == 0; },
                   handshakeTimeout_))
        return false;
    port_->writeControl(kInit);
    if (!waitUntil("ERROR release 2", [&] { return (port_->readStatus() & kError) != 0; },
                   handshakeTimeout_))
        return false;

    return true;
}

ComMode ParallelLink::sync()
{
    if (!isOpen())
        throw Error(ErrorCode::NotConnected, "Parallel port is not open");

    port_->writeControl(kInit);
    port_->writeData(0);

    const bool tryOld = !preferred_ || *preferred_ == ComMode::Old;
    const bool tryNew = !preferred_ || *preferred_ == ComMode::New;

    if (tryOld && syncOld()) {
        mode_ = ComMode::Old;
        return mode_;
    }
    reset();
    if (tryNew && syncNew()) {
        mode_ = ComMode::New;
        return mode_;
    }
    reset();
    throw Error(ErrorCode::Timeout,
                "No answer from the V64. Check the cable, the V64 power, and "
                "whether the unit is waiting for a PC transfer.");
}

void ParallelLink::waitReadyForEvenByte()
{
    const bool ok = mode_ == ComMode::Old
        ? waitUntil("AUTO.FX even",
                    [&] { return (port_->readControl() & kAutoLf) == 0; }, byteTimeout_)
        : waitUntil("SLCT even",
                    [&] { return (port_->readStatus() & kSlct) != 0; }, byteTimeout_);
    if (!ok)
        throw Error(ErrorCode::Timeout, "V64 did not accept the next byte");
}

void ParallelLink::waitReadyForOddByte()
{
    const bool ok = mode_ == ComMode::Old
        ? waitUntil("AUTO.FX odd",
                    [&] { return (port_->readControl() & kAutoLf) != 0; }, byteTimeout_)
        : waitUntil("SLCT odd",
                    [&] { return (port_->readStatus() & kSlct) == 0; }, byteTimeout_);
    if (!ok)
        throw Error(ErrorCode::Timeout, "V64 did not release the byte handshake");
}

void ParallelLink::write(std::span<const uint8_t> data)
{
    if (!isOpen())
        throw Error(ErrorCode::NotConnected, "Parallel port is not open");

    // TPC continues the even/odd phase across calls, using STB in the control
    // register as the phase bit. Reading the register also works on the
    // open-collector SPP outputs used by the original cable.
    bool odd = (port_->readControl() & kStb) != 0;
    for (uint8_t value : data) {
        if (!odd) {
            waitReadyForEvenByte();
            port_->writeData(value);
            port_->writeControl(kInit | kStb);
            odd = true;
        } else {
            waitReadyForOddByte();
            port_->writeData(value);
            port_->writeControl(kInit);
            odd = false;
        }
    }
}

void ParallelLink::read(std::span<uint8_t> out)
{
    if (!isOpen())
        throw Error(ErrorCode::NotConnected, "Parallel port is not open");

    // Data is transferred on status D3..D6 in two nibbles; BUSY (D7) is the
    // strobe. D7 of the data register selects the direction.
    port_->writeData(kReadSelect);
    for (uint8_t& value : out) {
        if (!waitUntil("BUSY low nibble",
                       [&] { return (port_->readStatus() & kBusy) != 0; }, byteTimeout_)) {
            throw Error(ErrorCode::Timeout, "Timed out waiting for a data nibble");
        }
        const uint8_t low = (port_->readStatus() >> 3) & 0x0F;

        port_->writeData(kWriteSelect);
        if (!waitUntil("BUSY high nibble",
                       [&] { return (port_->readStatus() & kBusy) == 0; }, byteTimeout_)) {
            throw Error(ErrorCode::Timeout, "Timed out waiting for the high nibble");
        }
        const uint8_t high = (port_->readStatus() >> 3) & 0x0F;

        port_->writeData(kReadSelect);
        value = static_cast<uint8_t>(low | (high << 4));
    }
}

void ParallelLink::checkSync()
{
    if (!isOpen())
        throw Error(ErrorCode::NotConnected, "Parallel port is not open");

    const auto deadline = std::chrono::steady_clock::now() + handshakeTimeout_;
    unsigned spins = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const uint8_t control = port_->readControl();
        const uint8_t status = port_->readStatus();
        bool ready = false;
        if (mode_ == ComMode::Old)
            ready = ((control & 0x03) == 0x03) || ((control & 0x03) == 0x00);
        else
            ready = (((control & kStb) ^ ((status & kSlct) >> 4)) != 0);

        if (ready) {
            port_->writeData(0);
            if (waitUntil("BUSY release",
                          [&] { return (port_->readStatus() & kBusy) == 0; },
                          handshakeTimeout_)) {
                return;
            }
            break;
        }
        if ((++spins & 0x3F) == 0)
            std::this_thread::yield();
    }
    throw Error(ErrorCode::Timeout, "V64 did not acknowledge the command header");
}

} // namespace tpc2
