// SPDX-License-Identifier: MIT
//
// Minimal real-mode 8086/80186 interpreter. It is just complete enough to run
// small DOS programs built with Borland/Turbo C (small memory model), with
// port I/O and software interrupts routed to C++ callbacks. No timing, no
// protected mode, no FPU (ESC opcodes are decoded and ignored).
#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace dostrace {

class CpuError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Cpu8086 {
public:
    enum Reg16 { AX, CX, DX, BX, SP, BP, SI, DI };
    enum Seg { ES, CS, SS, DS };
    enum Flag : uint16_t {
        CF = 0x0001, PF = 0x0004, AF = 0x0010, ZF = 0x0040, SF = 0x0080,
        TF = 0x0100, IF = 0x0200, DF = 0x0400, OF = 0x0800,
    };

    Cpu8086();

    // Executes one instruction (including its prefixes).
    void step();

    std::vector<uint8_t> mem; // 1 MiB, addresses wrap like an 8086
    uint16_t r[8] {};
    uint16_t s[4] {};
    uint16_t ip = 0;
    uint16_t flags = 0x0002;
    bool halted = false;
    uint64_t steps = 0;

    std::function<uint8_t(uint16_t port)> in;
    std::function<void(uint16_t port, uint8_t value)> out;
    // Returns true when the interrupt was serviced in C++; otherwise the
    // interrupt is dispatched through the vector table in memory.
    std::function<bool(uint8_t number)> interrupt;

    static uint32_t linear(uint16_t seg, uint16_t off)
    {
        return ((static_cast<uint32_t>(seg) << 4) + off) & 0xFFFFFu;
    }
    uint8_t read8(uint32_t a) const { return mem[a & 0xFFFFFu]; }
    uint16_t read16(uint32_t a) const
    {
        return static_cast<uint16_t>(read8(a) | (read8(a + 1) << 8));
    }
    void write8(uint32_t a, uint8_t v) { mem[a & 0xFFFFFu] = v; }
    void write16(uint32_t a, uint16_t v)
    {
        write8(a, static_cast<uint8_t>(v));
        write8(a + 1, static_cast<uint8_t>(v >> 8));
    }

    uint8_t reg8(int i) const
    {
        return i < 4 ? static_cast<uint8_t>(r[i]) : static_cast<uint8_t>(r[i - 4] >> 8);
    }
    void setReg8(int i, uint8_t v)
    {
        if (i < 4)
            r[i] = static_cast<uint16_t>((r[i] & 0xFF00) | v);
        else
            r[i - 4] = static_cast<uint16_t>((r[i - 4] & 0x00FF) | (v << 8));
    }

    void setFlag(uint16_t f, bool on) { flags = on ? (flags | f) : (flags & ~f); }
    bool flag(uint16_t f) const { return (flags & f) != 0; }

    void push(uint16_t v);
    uint16_t pop();
    void raise(uint8_t number); // software or exception interrupt

    std::string where() const;

private:
    struct ModRM {
        int mod = 0;
        int reg = 0;
        int rm = 0;
        uint32_t addr = 0; // linear address when mod != 3
        uint16_t off = 0;
        uint16_t seg = 0;
    };

    uint8_t fetch8();
    uint16_t fetch16();
    ModRM decode();
    uint16_t readRM(const ModRM& m, bool w);
    void writeRM(const ModRM& m, bool w, uint16_t v);

    uint16_t alu(int op, uint16_t a, uint16_t b, bool w);
    uint16_t shift(int op, uint16_t v, unsigned count, bool w);
    void setSZP(uint16_t v, bool w);
    bool condition(int cc) const;
    void group3(const ModRM& m, bool w);
    void stringOp(uint8_t op);
    void execute(uint8_t op);

    int segOverride_ = -1;
    int rep_ = 0; // 0 none, 0xF2 repne, 0xF3 rep/repe
    uint16_t startIp_ = 0;
    uint16_t startCs_ = 0;
};

} // namespace dostrace
