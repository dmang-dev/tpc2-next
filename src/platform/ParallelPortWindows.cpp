// SPDX-License-Identifier: MIT
#include "platform/ParallelPort.h"

#include "core/Error.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstdio>

namespace tpc2 {
namespace {

using Inp32Fn = short(__stdcall*)(short);
using Out32Fn = void(__stdcall*)(short, short);
using DriverOpenFn = BOOL(__stdcall*)();

class InpOutPort final : public ParallelPort {
public:
    ~InpOutPort() override { close(); }

    void open(uint16_t base) override
    {
        close();
        module_ = LoadLibraryW(L"inpoutx64.dll");
        if (!module_)
            module_ = LoadLibraryW(L"inpout32.dll");
        if (!module_) {
            throw Error(ErrorCode::Unsupported,
                        "InpOut32/InpOutx64 is not installed. Place inpoutx64.dll "
                        "next to tpc2.exe (or in System32) and install its kernel "
                        "driver. The USB bridge backend is recommended instead.");
        }
        inp_ = reinterpret_cast<Inp32Fn>(
            reinterpret_cast<void*>(GetProcAddress(module_, "Inp32")));
        out_ = reinterpret_cast<Out32Fn>(
            reinterpret_cast<void*>(GetProcAddress(module_, "Out32")));
        auto driverOpen = reinterpret_cast<DriverOpenFn>(
            reinterpret_cast<void*>(GetProcAddress(module_, "IsInpOutDriverOpen")));
        if (!inp_ || !out_)
            throw Error(ErrorCode::Unsupported,
                        "The loaded InpOut DLL does not export Inp32/Out32");
        if (driverOpen && !driverOpen()) {
            throw Error(ErrorCode::Unsupported,
                        "The InpOut kernel driver is not running. Install it from "
                        "an elevated command prompt, then reconnect.");
        }
        base_ = base;
        open_ = true;
    }

    void close() override
    {
        open_ = false;
        inp_ = nullptr;
        out_ = nullptr;
        if (module_) {
            FreeLibrary(module_);
            module_ = nullptr;
        }
    }

    [[nodiscard]] bool isOpen() const override { return open_; }

    void writeData(uint8_t value) override
    {
        ensureOpen();
        out_(static_cast<short>(base_), value);
    }

    [[nodiscard]] uint8_t readStatus() override
    {
        ensureOpen();
        return static_cast<uint8_t>(inp_(static_cast<short>(base_ + 1)) & 0xFF);
    }

    [[nodiscard]] uint8_t readControl() override
    {
        ensureOpen();
        return static_cast<uint8_t>(inp_(static_cast<short>(base_ + 2)) & 0xFF);
    }

    void writeControl(uint8_t value) override
    {
        ensureOpen();
        out_(static_cast<short>(base_ + 2), value);
    }

    [[nodiscard]] std::string description() const override
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "LPT at 0x%03X (InpOut)", base_);
        return buf;
    }

private:
    void ensureOpen() const
    {
        if (!open_)
            throw Error(ErrorCode::NotConnected, "Parallel port is not open");
    }

    HMODULE module_ = nullptr;
    Inp32Fn inp_ = nullptr;
    Out32Fn out_ = nullptr;
    uint16_t base_ = 0;
    bool open_ = false;
};

} // namespace

std::vector<PortDescriptor> ParallelPort::enumerate()
{
    const std::array<std::pair<const char*, uint16_t>, 3> known = { {
        { "LPT1", 0x378 },
        { "LPT2", 0x278 },
        { "LPT3", 0x3BC },
    } };

    bool driver = false;
    if (HMODULE mod = LoadLibraryW(L"inpoutx64.dll")) {
        auto fn = reinterpret_cast<DriverOpenFn>(
            reinterpret_cast<void*>(GetProcAddress(mod, "IsInpOutDriverOpen")));
        driver = !fn || fn();
        FreeLibrary(mod);
    } else if (HMODULE mod32 = LoadLibraryW(L"inpout32.dll")) {
        auto fn = reinterpret_cast<DriverOpenFn>(
            reinterpret_cast<void*>(GetProcAddress(mod32, "IsInpOutDriverOpen")));
        driver = !fn || fn();
        FreeLibrary(mod32);
    }

    std::vector<PortDescriptor> ports;
    for (const auto& [name, base] : known)
        ports.push_back(PortDescriptor { name, base, driver, driver ? "ready" : "InpOut driver not available" });
    return ports;
}

std::unique_ptr<ParallelPort> ParallelPort::create()
{
    return std::make_unique<InpOutPort>();
}

bool ParallelPort::supported()
{
    return true;
}

} // namespace tpc2
