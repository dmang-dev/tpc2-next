// SPDX-License-Identifier: MIT
//
// Just enough of MS-DOS to run a small Borland C program: MZ loading, a PSP
// with a command tail, an in-memory file system, a scripted console and the
// INT 21h/10h/16h/1Ah services the Borland C++ 3.x runtime uses. The BIOS
// data area advertises one printer port at 0x378.
#pragma once

#include "Cpu8086.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace dostrace {

class DosMachine {
public:
    DosMachine();

    void load(const std::vector<uint8_t>& exe, const std::string& programName,
              const std::string& commandTail);
    // Runs until the program exits. Throws CpuError on unsupported code or
    // when maxSteps instructions have executed.
    int run(uint64_t maxSteps = 2'000'000'000ull);

    std::map<std::string, std::vector<uint8_t>> files; // upper-case names
    std::string stdinText;
    std::string console;
    std::vector<std::string> log; // unexpected services and ports

    std::function<uint8_t(uint16_t)> portIn;
    std::function<void(uint16_t, uint8_t)> portOut;

    Cpu8086 cpu;

private:
    struct Handle {
        std::string name;
        std::size_t pos = 0;
    };

    bool handleInterrupt(uint8_t number);
    void dos();
    void video();
    void keyboard();
    std::string readString(uint16_t seg, uint16_t off) const;
    static std::string normalise(const std::string& path);
    void fillDta(const std::string& name);
    int stdinChar();
    void fail(uint16_t code);
    void ok();

    std::map<int, Handle> handles_;
    uint16_t psp_ = 0;
    uint16_t memTop_ = 0xA000;
    uint16_t nextAlloc_ = 0;
    uint32_t dta_ = 0;
    std::vector<std::string> findList_;
    std::size_t findIndex_ = 0;
    std::size_t stdinPos_ = 0;
    uint32_t ticks_ = 0;
    int exitCode_ = -1;
};

} // namespace dostrace
