// SPDX-License-Identifier: MIT
#include "Cpu8086.h"

#include <cstdio>

namespace dostrace {
namespace {

bool parity(uint8_t v)
{
    v ^= static_cast<uint8_t>(v >> 4);
    v ^= static_cast<uint8_t>(v >> 2);
    v ^= static_cast<uint8_t>(v >> 1);
    return (v & 1) == 0;
}

uint16_t signBit(bool w) { return w ? 0x8000 : 0x80; }
uint16_t mask(bool w) { return w ? 0xFFFF : 0xFF; }

} // namespace

Cpu8086::Cpu8086()
    : mem(0x100000, 0)
{
}

std::string Cpu8086::where() const
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04X:%04X (op %02X %02X)", startCs_, startIp_,
                  read8(linear(startCs_, startIp_)),
                  read8(linear(startCs_, static_cast<uint16_t>(startIp_ + 1))));
    return buf;
}

uint8_t Cpu8086::fetch8()
{
    const uint8_t v = read8(linear(s[CS], ip));
    ++ip;
    return v;
}

uint16_t Cpu8086::fetch16()
{
    const uint8_t lo = fetch8();
    const uint8_t hi = fetch8();
    return static_cast<uint16_t>(lo | (hi << 8));
}

void Cpu8086::push(uint16_t v)
{
    r[SP] = static_cast<uint16_t>(r[SP] - 2);
    write16(linear(s[SS], r[SP]), v);
}

uint16_t Cpu8086::pop()
{
    const uint16_t v = read16(linear(s[SS], r[SP]));
    r[SP] = static_cast<uint16_t>(r[SP] + 2);
    return v;
}

void Cpu8086::raise(uint8_t number)
{
    if (interrupt && interrupt(number))
        return;
    const uint16_t off = read16(number * 4u);
    const uint16_t seg = read16(number * 4u + 2);
    if (off == 0 && seg == 0) {
        char buf[48];
        std::snprintf(buf, sizeof buf, "unhandled interrupt %02Xh", number);
        throw CpuError(std::string(buf) + " at " + where());
    }
    push(static_cast<uint16_t>(flags | 0x0002));
    push(s[CS]);
    push(ip);
    setFlag(IF, false);
    setFlag(TF, false);
    s[CS] = seg;
    ip = off;
}

Cpu8086::ModRM Cpu8086::decode()
{
    ModRM m;
    const uint8_t b = fetch8();
    m.mod = b >> 6;
    m.reg = (b >> 3) & 7;
    m.rm = b & 7;
    if (m.mod == 3)
        return m;

    uint16_t off = 0;
    int seg = DS;
    switch (m.rm) {
    case 0: off = static_cast<uint16_t>(r[BX] + r[SI]); break;
    case 1: off = static_cast<uint16_t>(r[BX] + r[DI]); break;
    case 2: off = static_cast<uint16_t>(r[BP] + r[SI]); seg = SS; break;
    case 3: off = static_cast<uint16_t>(r[BP] + r[DI]); seg = SS; break;
    case 4: off = r[SI]; break;
    case 5: off = r[DI]; break;
    case 6:
        if (m.mod == 0) {
            off = fetch16();
        } else {
            off = r[BP];
            seg = SS;
        }
        break;
    case 7: off = r[BX]; break;
    }
    if (m.mod == 1)
        off = static_cast<uint16_t>(off + static_cast<int8_t>(fetch8()));
    else if (m.mod == 2)
        off = static_cast<uint16_t>(off + fetch16());
    if (segOverride_ >= 0)
        seg = segOverride_;
    m.off = off;
    m.seg = s[seg];
    m.addr = linear(m.seg, off);
    return m;
}

uint16_t Cpu8086::readRM(const ModRM& m, bool w)
{
    if (m.mod == 3)
        return w ? r[m.rm] : reg8(m.rm);
    return w ? read16(m.addr) : read8(m.addr);
}

void Cpu8086::writeRM(const ModRM& m, bool w, uint16_t v)
{
    if (m.mod == 3) {
        if (w)
            r[m.rm] = v;
        else
            setReg8(m.rm, static_cast<uint8_t>(v));
        return;
    }
    if (w)
        write16(m.addr, v);
    else
        write8(m.addr, static_cast<uint8_t>(v));
}

void Cpu8086::setSZP(uint16_t v, bool w)
{
    v &= mask(w);
    setFlag(ZF, v == 0);
    setFlag(SF, (v & signBit(w)) != 0);
    setFlag(PF, parity(static_cast<uint8_t>(v)));
}

uint16_t Cpu8086::alu(int op, uint16_t a, uint16_t b, bool w)
{
    const uint32_t m = mask(w);
    const uint32_t sb = signBit(w);
    a &= m;
    b &= m;
    uint32_t res = 0;
    switch (op) {
    case 0: // ADD
    case 2: { // ADC
        const uint32_t c = (op == 2 && flag(CF)) ? 1 : 0;
        res = a + b + c;
        setFlag(CF, res > m);
        setFlag(OF, ((a ^ res) & (b ^ res) & sb) != 0);
        setFlag(AF, ((a ^ b ^ res) & 0x10) != 0);
        break;
    }
    case 3: // SBB
    case 5: // SUB
    case 7: { // CMP
        const uint32_t c = (op == 3 && flag(CF)) ? 1 : 0;
        res = (a - b - c) & 0x1FFFF;
        setFlag(CF, static_cast<uint32_t>(b) + c > a);
        setFlag(OF, ((a ^ b) & (a ^ res) & sb) != 0);
        setFlag(AF, ((a ^ b ^ res) & 0x10) != 0);
        break;
    }
    case 1: res = a | b; setFlag(CF, false); setFlag(OF, false); setFlag(AF, false); break;
    case 4: res = a & b; setFlag(CF, false); setFlag(OF, false); setFlag(AF, false); break;
    case 6: res = a ^ b; setFlag(CF, false); setFlag(OF, false); setFlag(AF, false); break;
    }
    res &= m;
    setSZP(static_cast<uint16_t>(res), w);
    return static_cast<uint16_t>(res);
}

uint16_t Cpu8086::shift(int op, uint16_t v, unsigned count, bool w)
{
    count &= 0x1F;
    if (count == 0)
        return v;
    const uint32_t m = mask(w);
    const uint32_t sb = signBit(w);
    const unsigned bits = w ? 16 : 8;
    uint32_t x = v & m;
    bool cf = flag(CF);
    for (unsigned i = 0; i < count; ++i) {
        switch (op) {
        case 0: // ROL
            cf = (x & sb) != 0;
            x = ((x << 1) | (cf ? 1 : 0)) & m;
            break;
        case 1: // ROR
            cf = (x & 1) != 0;
            x = (x >> 1) | (cf ? sb : 0);
            break;
        case 2: { // RCL
            const bool carry = (x & sb) != 0;
            x = ((x << 1) | (cf ? 1 : 0)) & m;
            cf = carry;
            break;
        }
        case 3: { // RCR
            const bool carry = (x & 1) != 0;
            x = (x >> 1) | (cf ? sb : 0);
            cf = carry;
            break;
        }
        case 4: // SHL
        case 6: // SAL (undocumented alias)
            cf = (x & sb) != 0;
            x = (x << 1) & m;
            break;
        case 5: // SHR
            cf = (x & 1) != 0;
            x >>= 1;
            break;
        case 7: // SAR
            cf = (x & 1) != 0;
            x = (x >> 1) | (x & sb);
            break;
        }
    }
    (void)bits;
    setFlag(CF, cf);
    const bool msb = (x & sb) != 0;
    switch (op) {
    case 0: case 2: case 4: case 6: setFlag(OF, msb != cf); break;
    case 1: case 3: setFlag(OF, msb != ((x & (sb >> 1)) != 0)); break;
    case 5: setFlag(OF, (v & sb) != 0); break;
    case 7: setFlag(OF, false); break;
    }
    if (op >= 4)
        setSZP(static_cast<uint16_t>(x), w);
    return static_cast<uint16_t>(x);
}

bool Cpu8086::condition(int cc) const
{
    bool result = false;
    switch (cc >> 1) {
    case 0: result = flag(OF); break;
    case 1: result = flag(CF); break;
    case 2: result = flag(ZF); break;
    case 3: result = flag(CF) || flag(ZF); break;
    case 4: result = flag(SF); break;
    case 5: result = flag(PF); break;
    case 6: result = flag(SF) != flag(OF); break;
    case 7: result = flag(ZF) || (flag(SF) != flag(OF)); break;
    }
    return (cc & 1) ? !result : result;
}

void Cpu8086::group3(const ModRM& m, bool w)
{
    const uint16_t v = readRM(m, w);
    switch (m.reg) {
    case 0:
    case 1: { // TEST r/m, imm
        const uint16_t imm = w ? fetch16() : fetch8();
        alu(4, v, imm, w);
        break;
    }
    case 2: writeRM(m, w, static_cast<uint16_t>(~v)); break; // NOT
    case 3: { // NEG
        const uint16_t res = alu(5, 0, v, w);
        setFlag(CF, (v & mask(w)) != 0);
        writeRM(m, w, res);
        break;
    }
    case 4: // MUL
        if (!w) {
            const uint16_t res = static_cast<uint16_t>(reg8(0) * (v & 0xFF));
            r[AX] = res;
            setFlag(CF, (res & 0xFF00) != 0);
            setFlag(OF, (res & 0xFF00) != 0);
        } else {
            const uint32_t res = static_cast<uint32_t>(r[AX]) * v;
            r[AX] = static_cast<uint16_t>(res);
            r[DX] = static_cast<uint16_t>(res >> 16);
            setFlag(CF, r[DX] != 0);
            setFlag(OF, r[DX] != 0);
        }
        break;
    case 5: // IMUL
        if (!w) {
            const int16_t res = static_cast<int16_t>(static_cast<int8_t>(reg8(0))
                                                     * static_cast<int8_t>(v));
            r[AX] = static_cast<uint16_t>(res);
            const bool big = res != static_cast<int8_t>(res);
            setFlag(CF, big);
            setFlag(OF, big);
        } else {
            const int32_t res = static_cast<int32_t>(static_cast<int16_t>(r[AX]))
                * static_cast<int16_t>(v);
            r[AX] = static_cast<uint16_t>(res);
            r[DX] = static_cast<uint16_t>(static_cast<uint32_t>(res) >> 16);
            const bool big = res != static_cast<int16_t>(res);
            setFlag(CF, big);
            setFlag(OF, big);
        }
        break;
    case 6: // DIV
        if (!w) {
            const uint16_t d = v & 0xFF;
            if (d == 0 || r[AX] / d > 0xFF) {
                raise(0);
                return;
            }
            const uint16_t q = r[AX] / d;
            const uint16_t rem = r[AX] % d;
            r[AX] = static_cast<uint16_t>((rem << 8) | q);
        } else {
            const uint32_t n = (static_cast<uint32_t>(r[DX]) << 16) | r[AX];
            if (v == 0 || n / v > 0xFFFF) {
                raise(0);
                return;
            }
            r[AX] = static_cast<uint16_t>(n / v);
            r[DX] = static_cast<uint16_t>(n % v);
        }
        break;
    case 7: // IDIV
        if (!w) {
            const int16_t n = static_cast<int16_t>(r[AX]);
            const int16_t d = static_cast<int8_t>(v);
            if (d == 0 || n / d > 127 || n / d < -128) {
                raise(0);
                return;
            }
            const int16_t q = static_cast<int16_t>(n / d);
            const int16_t rem = static_cast<int16_t>(n % d);
            r[AX] = static_cast<uint16_t>(((rem & 0xFF) << 8) | (q & 0xFF));
        } else {
            const int32_t n = static_cast<int32_t>((static_cast<uint32_t>(r[DX]) << 16) | r[AX]);
            const int32_t d = static_cast<int16_t>(v);
            if (d == 0 || n / d > 32767 || n / d < -32768) {
                raise(0);
                return;
            }
            r[AX] = static_cast<uint16_t>(n / d);
            r[DX] = static_cast<uint16_t>(n % d);
        }
        break;
    }
}

void Cpu8086::stringOp(uint8_t op)
{
    const bool w = (op & 1) != 0;
    const int delta = (flag(DF) ? -1 : 1) * (w ? 2 : 1);
    const uint16_t srcSeg = s[segOverride_ >= 0 ? segOverride_ : DS];
    const bool cmp = op == 0xA6 || op == 0xA7 || op == 0xAE || op == 0xAF;

    auto once = [&] {
        switch (op & 0xFE) {
        case 0xA4: { // MOVS
            const uint16_t v = w ? read16(linear(srcSeg, r[SI])) : read8(linear(srcSeg, r[SI]));
            if (w)
                write16(linear(s[ES], r[DI]), v);
            else
                write8(linear(s[ES], r[DI]), static_cast<uint8_t>(v));
            r[SI] = static_cast<uint16_t>(r[SI] + delta);
            r[DI] = static_cast<uint16_t>(r[DI] + delta);
            break;
        }
        case 0xA6: { // CMPS
            const uint16_t a = w ? read16(linear(srcSeg, r[SI])) : read8(linear(srcSeg, r[SI]));
            const uint16_t b = w ? read16(linear(s[ES], r[DI])) : read8(linear(s[ES], r[DI]));
            alu(7, a, b, w);
            r[SI] = static_cast<uint16_t>(r[SI] + delta);
            r[DI] = static_cast<uint16_t>(r[DI] + delta);
            break;
        }
        case 0xAA: // STOS
            if (w)
                write16(linear(s[ES], r[DI]), r[AX]);
            else
                write8(linear(s[ES], r[DI]), reg8(0));
            r[DI] = static_cast<uint16_t>(r[DI] + delta);
            break;
        case 0xAC: // LODS
            if (w)
                r[AX] = read16(linear(srcSeg, r[SI]));
            else
                setReg8(0, read8(linear(srcSeg, r[SI])));
            r[SI] = static_cast<uint16_t>(r[SI] + delta);
            break;
        case 0xAE: { // SCAS
            const uint16_t b = w ? read16(linear(s[ES], r[DI])) : read8(linear(s[ES], r[DI]));
            alu(7, w ? r[AX] : reg8(0), b, w);
            r[DI] = static_cast<uint16_t>(r[DI] + delta);
            break;
        }
        }
    };

    if (!rep_) {
        once();
        return;
    }
    while (r[CX] != 0) {
        once();
        r[CX] = static_cast<uint16_t>(r[CX] - 1);
        if (cmp) {
            if (rep_ == 0xF3 && !flag(ZF))
                break;
            if (rep_ == 0xF2 && flag(ZF))
                break;
        }
    }
}

void Cpu8086::step()
{
    startCs_ = s[CS];
    startIp_ = ip;
    segOverride_ = -1;
    rep_ = 0;
    uint8_t op = 0;
    for (;;) {
        op = fetch8();
        if (op == 0x26) segOverride_ = ES;
        else if (op == 0x2E) segOverride_ = CS;
        else if (op == 0x36) segOverride_ = SS;
        else if (op == 0x3E) segOverride_ = DS;
        else if (op == 0xF0) { /* LOCK */ }
        else if (op == 0xF2 || op == 0xF3) rep_ = op;
        else break;
    }
    execute(op);
    ++steps;
}

void Cpu8086::execute(uint8_t op)
{
    // ALU r/m forms and accumulator-immediate forms: 00-3F except prefixes,
    // segment push/pop and BCD adjust.
    if (op < 0x40 && (op & 7) < 6) {
        const int aluOp = op >> 3;
        const bool w = (op & 1) != 0;
        switch (op & 7) {
        case 0: case 1: { // r/m, reg
            const ModRM m = decode();
            const uint16_t res = alu(aluOp, readRM(m, w), w ? r[m.reg] : reg8(m.reg), w);
            if (aluOp != 7)
                writeRM(m, w, res);
            return;
        }
        case 2: case 3: { // reg, r/m
            const ModRM m = decode();
            const uint16_t res = alu(aluOp, w ? r[m.reg] : reg8(m.reg), readRM(m, w), w);
            if (aluOp != 7) {
                if (w) r[m.reg] = res;
                else setReg8(m.reg, static_cast<uint8_t>(res));
            }
            return;
        }
        case 4: { // AL, imm8
            const uint16_t res = alu(aluOp, reg8(0), fetch8(), false);
            if (aluOp != 7)
                setReg8(0, static_cast<uint8_t>(res));
            return;
        }
        case 5: { // AX, imm16
            const uint16_t res = alu(aluOp, r[AX], fetch16(), true);
            if (aluOp != 7)
                r[AX] = res;
            return;
        }
        }
    }

    switch (op) {
    case 0x06: push(s[ES]); return;
    case 0x07: s[ES] = pop(); return;
    case 0x0E: push(s[CS]); return;
    case 0x16: push(s[SS]); return;
    case 0x17: s[SS] = pop(); return;
    case 0x1E: push(s[DS]); return;
    case 0x1F: s[DS] = pop(); return;

    case 0x27: case 0x2F: { // DAA / DAS
        uint8_t al = reg8(0);
        const bool oldCf = flag(CF);
        const uint8_t oldAl = al;
        bool cf = false;
        if ((al & 0x0F) > 9 || flag(AF)) {
            al = static_cast<uint8_t>(op == 0x27 ? al + 6 : al - 6);
            cf = oldCf || (op == 0x27 ? oldAl > 0xF9 : oldAl < 6);
            setFlag(AF, true);
        } else {
            setFlag(AF, false);
        }
        if (oldAl > 0x99 || oldCf) {
            al = static_cast<uint8_t>(op == 0x27 ? al + 0x60 : al - 0x60);
            cf = true;
        }
        setReg8(0, al);
        setFlag(CF, cf);
        setSZP(al, false);
        return;
    }
    case 0x37: case 0x3F: { // AAA / AAS
        if ((reg8(0) & 0x0F) > 9 || flag(AF)) {
            if (op == 0x37) {
                r[AX] = static_cast<uint16_t>(r[AX] + 0x106);
            } else {
                setReg8(0, static_cast<uint8_t>(reg8(0) - 6));
                setReg8(4, static_cast<uint8_t>(reg8(4) - 1));
            }
            setFlag(AF, true);
            setFlag(CF, true);
        } else {
            setFlag(AF, false);
            setFlag(CF, false);
        }
        setReg8(0, reg8(0) & 0x0F);
        return;
    }
    default:
        break;
    }

    if (op >= 0x40 && op <= 0x4F) { // INC/DEC r16
        const int i = op & 7;
        const bool cf = flag(CF);
        r[i] = alu(op < 0x48 ? 0 : 5, r[i], 1, true);
        setFlag(CF, cf);
        return;
    }
    if (op >= 0x50 && op <= 0x57) {
        push(r[op & 7]);
        return;
    }
    if (op >= 0x58 && op <= 0x5F) {
        r[op & 7] = pop();
        return;
    }
    if (op >= 0x70 && op <= 0x7F) {
        const int8_t d = static_cast<int8_t>(fetch8());
        if (condition(op & 0x0F))
            ip = static_cast<uint16_t>(ip + d);
        return;
    }
    if (op >= 0x91 && op <= 0x97) {
        const uint16_t t = r[AX];
        r[AX] = r[op & 7];
        r[op & 7] = t;
        return;
    }
    if (op >= 0xB0 && op <= 0xB7) {
        setReg8(op & 7, fetch8());
        return;
    }
    if (op >= 0xB8 && op <= 0xBF) {
        r[op & 7] = fetch16();
        return;
    }
    if (op >= 0xD8 && op <= 0xDF) { // ESC (FPU): decode and ignore
        decode();
        return;
    }
    if (op >= 0xA4 && op <= 0xAF && op != 0xA8 && op != 0xA9) {
        stringOp(op);
        return;
    }

    switch (op) {
    case 0x60: { // PUSHA
        const uint16_t sp = r[SP];
        push(r[AX]); push(r[CX]); push(r[DX]); push(r[BX]);
        push(sp); push(r[BP]); push(r[SI]); push(r[DI]);
        return;
    }
    case 0x61: // POPA
        r[DI] = pop(); r[SI] = pop(); r[BP] = pop(); pop();
        r[BX] = pop(); r[DX] = pop(); r[CX] = pop(); r[AX] = pop();
        return;
    case 0x62: decode(); return; // BOUND: assume in range
    case 0x68: push(fetch16()); return;
    case 0x6A: push(static_cast<uint16_t>(static_cast<int8_t>(fetch8()))); return;
    case 0x69: case 0x6B: { // IMUL r16, r/m16, imm
        const ModRM m = decode();
        const int32_t a = static_cast<int16_t>(readRM(m, true));
        const int32_t b = op == 0x69 ? static_cast<int16_t>(fetch16())
                                     : static_cast<int8_t>(fetch8());
        const int32_t res = a * b;
        r[m.reg] = static_cast<uint16_t>(res);
        const bool big = res != static_cast<int16_t>(res);
        setFlag(CF, big);
        setFlag(OF, big);
        return;
    }

    case 0x80: case 0x81: case 0x82: case 0x83: {
        const bool w = op == 0x81 || op == 0x83;
        const ModRM m = decode();
        uint16_t imm = 0;
        if (op == 0x81)
            imm = fetch16();
        else if (op == 0x83)
            imm = static_cast<uint16_t>(static_cast<int8_t>(fetch8()));
        else
            imm = fetch8();
        const uint16_t res = alu(m.reg, readRM(m, w), imm, w);
        if (m.reg != 7)
            writeRM(m, w, res);
        return;
    }
    case 0x84: case 0x85: {
        const bool w = op == 0x85;
        const ModRM m = decode();
        alu(4, readRM(m, w), w ? r[m.reg] : reg8(m.reg), w);
        return;
    }
    case 0x86: case 0x87: {
        const bool w = op == 0x87;
        const ModRM m = decode();
        const uint16_t a = readRM(m, w);
        const uint16_t b = w ? r[m.reg] : reg8(m.reg);
        writeRM(m, w, b);
        if (w) r[m.reg] = a;
        else setReg8(m.reg, static_cast<uint8_t>(a));
        return;
    }
    case 0x88: case 0x89: {
        const bool w = op == 0x89;
        const ModRM m = decode();
        writeRM(m, w, w ? r[m.reg] : reg8(m.reg));
        return;
    }
    case 0x8A: case 0x8B: {
        const bool w = op == 0x8B;
        const ModRM m = decode();
        const uint16_t v = readRM(m, w);
        if (w) r[m.reg] = v;
        else setReg8(m.reg, static_cast<uint8_t>(v));
        return;
    }
    case 0x8C: { const ModRM m = decode(); writeRM(m, true, s[m.reg & 3]); return; }
    case 0x8D: { const ModRM m = decode(); r[m.reg] = m.off; return; }
    case 0x8E: { const ModRM m = decode(); s[m.reg & 3] = readRM(m, true); return; }
    case 0x8F: { const ModRM m = decode(); writeRM(m, true, pop()); return; }
    case 0x90: return;
    case 0x98: r[AX] = static_cast<uint16_t>(static_cast<int8_t>(reg8(0))); return;
    case 0x99: r[DX] = (r[AX] & 0x8000) ? 0xFFFF : 0; return;
    case 0x9A: {
        const uint16_t off = fetch16();
        const uint16_t seg = fetch16();
        push(s[CS]);
        push(ip);
        s[CS] = seg;
        ip = off;
        return;
    }
    case 0x9B: return; // WAIT
    case 0x9C: push(static_cast<uint16_t>((flags & 0x0FD5) | 0x0002)); return;
    case 0x9D: flags = static_cast<uint16_t>((pop() & 0x0FD5) | 0x0002); return;
    case 0x9E: flags = static_cast<uint16_t>((flags & 0xFF00) | (reg8(4) & 0xD5) | 0x02); return;
    case 0x9F: setReg8(4, static_cast<uint8_t>(flags)); return;
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: {
        const uint16_t off = fetch16();
        const uint32_t a = linear(s[segOverride_ >= 0 ? segOverride_ : DS], off);
        if (op == 0xA0) setReg8(0, read8(a));
        else if (op == 0xA1) r[AX] = read16(a);
        else if (op == 0xA2) write8(a, reg8(0));
        else write16(a, r[AX]);
        return;
    }
    case 0xA8: alu(4, reg8(0), fetch8(), false); return;
    case 0xA9: alu(4, r[AX], fetch16(), true); return;

    case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
        const bool w = (op & 1) != 0;
        const ModRM m = decode();
        unsigned count = 1;
        if (op <= 0xC1) count = fetch8();
        else if (op >= 0xD2) count = reg8(1);
        writeRM(m, w, shift(m.reg, readRM(m, w), count, w));
        return;
    }
    case 0xC2: { const uint16_t n = fetch16(); ip = pop(); r[SP] = static_cast<uint16_t>(r[SP] + n); return; }
    case 0xC3: ip = pop(); return;
    case 0xC4: case 0xC5: {
        const ModRM m = decode();
        r[m.reg] = read16(m.addr);
        s[op == 0xC4 ? ES : DS] = read16(linear(m.seg, static_cast<uint16_t>(m.off + 2)));
        return;
    }
    case 0xC6: case 0xC7: {
        const bool w = op == 0xC7;
        const ModRM m = decode();
        writeRM(m, w, w ? fetch16() : fetch8());
        return;
    }
    case 0xC8: { // ENTER
        const uint16_t size = fetch16();
        const uint8_t level = fetch8() & 0x1F;
        push(r[BP]);
        const uint16_t frame = r[SP];
        for (unsigned i = 1; i < level; ++i) {
            r[BP] = static_cast<uint16_t>(r[BP] - 2);
            push(read16(linear(s[SS], r[BP])));
        }
        if (level > 0)
            push(frame);
        r[BP] = frame;
        r[SP] = static_cast<uint16_t>(r[SP] - size);
        return;
    }
    case 0xC9: r[SP] = r[BP]; r[BP] = pop(); return; // LEAVE
    case 0xCA: { const uint16_t n = fetch16(); ip = pop(); s[CS] = pop(); r[SP] = static_cast<uint16_t>(r[SP] + n); return; }
    case 0xCB: ip = pop(); s[CS] = pop(); return;
    case 0xCC: raise(3); return;
    case 0xCD: raise(fetch8()); return;
    case 0xCE: if (flag(OF)) raise(4); return;
    case 0xCF: ip = pop(); s[CS] = pop(); flags = static_cast<uint16_t>((pop() & 0x0FD5) | 0x0002); return;
    case 0xD4: { // AAM
        const uint8_t base = fetch8();
        if (base == 0) { raise(0); return; }
        const uint8_t al = reg8(0);
        setReg8(4, static_cast<uint8_t>(al / base));
        setReg8(0, static_cast<uint8_t>(al % base));
        setSZP(reg8(0), false);
        return;
    }
    case 0xD5: { // AAD
        const uint8_t base = fetch8();
        setReg8(0, static_cast<uint8_t>(reg8(4) * base + reg8(0)));
        setReg8(4, 0);
        setSZP(reg8(0), false);
        return;
    }
    case 0xD7: {
        const uint16_t seg = s[segOverride_ >= 0 ? segOverride_ : DS];
        setReg8(0, read8(linear(seg, static_cast<uint16_t>(r[BX] + reg8(0)))));
        return;
    }
    case 0xE0: case 0xE1: case 0xE2: {
        const int8_t d = static_cast<int8_t>(fetch8());
        r[CX] = static_cast<uint16_t>(r[CX] - 1);
        bool jump = r[CX] != 0;
        if (op == 0xE0) jump = jump && !flag(ZF);
        if (op == 0xE1) jump = jump && flag(ZF);
        if (jump)
            ip = static_cast<uint16_t>(ip + d);
        return;
    }
    case 0xE3: { const int8_t d = static_cast<int8_t>(fetch8()); if (r[CX] == 0) ip = static_cast<uint16_t>(ip + d); return; }
    case 0xE4: setReg8(0, in ? in(fetch8()) : 0xFF); return;
    case 0xE5: { const uint8_t p = fetch8(); r[AX] = static_cast<uint16_t>((in ? in(p) : 0xFF) | ((in ? in(static_cast<uint16_t>(p + 1)) : 0xFF) << 8)); return; }
    case 0xE6: { const uint8_t p = fetch8(); if (out) out(p, reg8(0)); return; }
    case 0xE7: { const uint8_t p = fetch8(); if (out) { out(p, reg8(0)); out(static_cast<uint16_t>(p + 1), reg8(4)); } return; }
    case 0xE8: { const uint16_t d = fetch16(); push(ip); ip = static_cast<uint16_t>(ip + d); return; }
    case 0xE9: { const uint16_t d = fetch16(); ip = static_cast<uint16_t>(ip + d); return; }
    case 0xEA: { const uint16_t off = fetch16(); s[CS] = fetch16(); ip = off; return; }
    case 0xEB: { const int8_t d = static_cast<int8_t>(fetch8()); ip = static_cast<uint16_t>(ip + d); return; }
    case 0xEC: setReg8(0, in ? in(r[DX]) : 0xFF); return;
    case 0xED: r[AX] = static_cast<uint16_t>((in ? in(r[DX]) : 0xFF) | ((in ? in(static_cast<uint16_t>(r[DX] + 1)) : 0xFF) << 8)); return;
    case 0xEE: if (out) out(r[DX], reg8(0)); return;
    case 0xEF: if (out) { out(r[DX], reg8(0)); out(static_cast<uint16_t>(r[DX] + 1), reg8(4)); } return;
    case 0xF4: halted = true; return;
    case 0xF5: setFlag(CF, !flag(CF)); return;
    case 0xF6: case 0xF7: { const ModRM m = decode(); group3(m, op == 0xF7); return; }
    case 0xF8: setFlag(CF, false); return;
    case 0xF9: setFlag(CF, true); return;
    case 0xFA: setFlag(IF, false); return;
    case 0xFB: setFlag(IF, true); return;
    case 0xFC: setFlag(DF, false); return;
    case 0xFD: setFlag(DF, true); return;
    case 0xFE: {
        const ModRM m = decode();
        const bool cf = flag(CF);
        if (m.reg > 1)
            break;
        writeRM(m, false, alu(m.reg == 0 ? 0 : 5, readRM(m, false), 1, false));
        setFlag(CF, cf);
        return;
    }
    case 0xFF: {
        const ModRM m = decode();
        switch (m.reg) {
        case 0: case 1: {
            const bool cf = flag(CF);
            writeRM(m, true, alu(m.reg == 0 ? 0 : 5, readRM(m, true), 1, true));
            setFlag(CF, cf);
            return;
        }
        case 2: { const uint16_t t = readRM(m, true); push(ip); ip = t; return; }
        case 3: {
            const uint16_t off = read16(m.addr);
            const uint16_t seg = read16(linear(m.seg, static_cast<uint16_t>(m.off + 2)));
            push(s[CS]);
            push(ip);
            s[CS] = seg;
            ip = off;
            return;
        }
        case 4: ip = readRM(m, true); return;
        case 5:
            ip = read16(m.addr);
            s[CS] = read16(linear(m.seg, static_cast<uint16_t>(m.off + 2)));
            return;
        case 6: push(readRM(m, true)); return;
        default: break;
        }
        break;
    }
    default:
        break;
    }
    throw CpuError("unsupported opcode at " + where());
}

} // namespace dostrace
