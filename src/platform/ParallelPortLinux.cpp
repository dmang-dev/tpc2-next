// SPDX-License-Identifier: MIT
#include "platform/ParallelPort.h"

#include "core/Error.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/io.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace tpc2 {
namespace {

class LinuxPort final : public ParallelPort {
public:
    ~LinuxPort() override { close(); }

    void open(uint16_t base) override
    {
        close();
        base_ = base;

#ifdef __linux__
        if (ioperm(base, 3, 1) == 0) {
            useIoperm_ = true;
            open_ = true;
            return;
        }
#endif

        fd_ = ::open("/dev/port", O_RDWR | O_SYNC);
        if (fd_ < 0) {
            throw Error(ErrorCode::Unsupported,
                        "Cannot access the parallel port. Run as root, or grant "
                        "access to /dev/port, or install the USB bridge.");
        }
        open_ = true;
    }

    void close() override
    {
#ifdef __linux__
        if (useIoperm_) {
            ioperm(base_, 3, 0);
            useIoperm_ = false;
        }
#endif
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        open_ = false;
    }

    [[nodiscard]] bool isOpen() const override { return open_; }

    void writeData(uint8_t value) override { writeRegister(0, value); }
    [[nodiscard]] uint8_t readStatus() override { return readRegister(1); }
    [[nodiscard]] uint8_t readControl() override { return readRegister(2); }
    void writeControl(uint8_t value) override { writeRegister(2, value); }

    [[nodiscard]] std::string description() const override
    {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "LPT at 0x%03X (%s)", base_,
                      useIoperm_ ? "ioperm" : "/dev/port");
        return buf;
    }

private:
    void writeRegister(int offset, uint8_t value)
    {
        ensureOpen();
        if (useIoperm_) {
            outb(value, static_cast<unsigned short>(base_ + offset));
        } else {
            const off_t pos = static_cast<off_t>(base_ + offset);
            if (pwrite(fd_, &value, 1, pos) != 1) {
                throw Error(ErrorCode::Io,
                            "pwrite(/dev/port) failed: " + std::string(std::strerror(errno)));
            }
        }
    }

    uint8_t readRegister(int offset)
    {
        ensureOpen();
        if (useIoperm_)
            return inb(static_cast<unsigned short>(base_ + offset));
        uint8_t value = 0;
        const off_t pos = static_cast<off_t>(base_ + offset);
        if (pread(fd_, &value, 1, pos) != 1) {
            throw Error(ErrorCode::Io,
                        "pread(/dev/port) failed: " + std::string(std::strerror(errno)));
        }
        return value;
    }

    void ensureOpen() const
    {
        if (!open_)
            throw Error(ErrorCode::NotConnected, "Parallel port is not open");
    }

    uint16_t base_ = 0;
    int fd_ = -1;
    bool useIoperm_ = false;
    bool open_ = false;
};

} // namespace

std::vector<PortDescriptor> ParallelPort::enumerate()
{
    const bool root = (geteuid() == 0);
    const bool devPort = (access("/dev/port", R_OK | W_OK) == 0);
    const std::array<std::pair<const char*, uint16_t>, 3> known = { {
        { "LPT1", 0x378 },
        { "LPT2", 0x278 },
        { "LPT3", 0x3BC },
    } };
    std::vector<PortDescriptor> ports;
    for (const auto& [name, base] : known) {
        ports.push_back(PortDescriptor {
            name, base, root || devPort,
            root ? "ready (root)" : (devPort ? "/dev/port writable" : "requires root or /dev/port access") });
    }
    return ports;
}

std::unique_ptr<ParallelPort> ParallelPort::create()
{
    return std::make_unique<LinuxPort>();
}

bool ParallelPort::supported()
{
    return true;
}

} // namespace tpc2
