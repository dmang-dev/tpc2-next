// SPDX-License-Identifier: MIT
#include "DosMachine.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace dostrace {
namespace {

constexpr uint16_t kEnvSeg = 0x0700;
constexpr uint16_t kPspSeg = 0x0800;

bool wildcardMatch(const std::string& pattern, const std::string& name)
{
    // DOS-style: '*' matches the rest of the base name or extension.
    auto split = [](const std::string& s) {
        const auto dot = s.find('.');
        return std::make_pair(s.substr(0, dot),
                              dot == std::string::npos ? std::string() : s.substr(dot + 1));
    };
    auto part = [](const std::string& p, const std::string& n) {
        std::size_t i = 0;
        for (; i < p.size(); ++i) {
            if (p[i] == '*')
                return true;
            if (i >= n.size())
                return false;
            if (p[i] != '?' && p[i] != n[i])
                return false;
        }
        return i == n.size();
    };
    const auto [pb, pe] = split(pattern);
    const auto [nb, ne] = split(name);
    return part(pb, nb) && part(pe, ne);
}

} // namespace

DosMachine::DosMachine()
{
    cpu.interrupt = [this](uint8_t number) { return handleInterrupt(number); };
    cpu.in = [this](uint16_t port) -> uint8_t {
        if (portIn)
            return portIn(port);
        return 0xFF;
    };
    cpu.out = [this](uint16_t port, uint8_t value) {
        if (portOut)
            portOut(port, value);
    };
}

std::string DosMachine::normalise(const std::string& path)
{
    std::string name = path;
    const auto cut = name.find_last_of("\\/:");
    if (cut != std::string::npos)
        name.erase(0, cut + 1);
    for (char& c : name)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return name;
}

std::string DosMachine::readString(uint16_t seg, uint16_t off) const
{
    std::string out;
    for (uint16_t i = 0; i < 128; ++i) {
        const uint8_t c = cpu.read8(Cpu8086::linear(seg, static_cast<uint16_t>(off + i)));
        if (c == 0)
            break;
        out.push_back(static_cast<char>(c));
    }
    return out;
}

void DosMachine::load(const std::vector<uint8_t>& exe, const std::string& programName,
                      const std::string& commandTail)
{
    if (exe.size() < 28 || exe[0] != 'M' || exe[1] != 'Z')
        throw CpuError("not an MZ executable");
    auto word = [&](std::size_t at) {
        return static_cast<uint16_t>(exe.at(at) | (exe.at(at + 1) << 8));
    };
    const uint16_t lastPage = word(2);
    const uint16_t pages = word(4);
    const uint16_t relocations = word(6);
    const uint16_t headerParas = word(8);
    const uint16_t ss = word(14);
    const uint16_t sp = word(16);
    const uint16_t ip = word(20);
    const uint16_t cs = word(22);
    const uint16_t relocTable = word(24);

    std::size_t imageEnd = static_cast<std::size_t>(pages) * 512;
    if (lastPage)
        imageEnd -= 512 - lastPage;
    const std::size_t headerSize = static_cast<std::size_t>(headerParas) * 16;
    if (imageEnd > exe.size() || headerSize > imageEnd)
        throw CpuError("truncated MZ executable");

    psp_ = kPspSeg;
    const uint16_t loadSeg = static_cast<uint16_t>(psp_ + 0x10);
    for (std::size_t i = headerSize; i < imageEnd; ++i)
        cpu.write8(Cpu8086::linear(loadSeg, 0) + static_cast<uint32_t>(i - headerSize), exe[i]);
    for (uint16_t i = 0; i < relocations; ++i) {
        const uint16_t off = word(relocTable + i * 4u);
        const uint16_t seg = word(relocTable + i * 4u + 2);
        const uint32_t at = Cpu8086::linear(static_cast<uint16_t>(loadSeg + seg), off);
        cpu.write16(at, static_cast<uint16_t>(cpu.read16(at) + loadSeg));
    }
    const uint16_t imageParas = static_cast<uint16_t>((imageEnd - headerSize + 15) / 16);
    nextAlloc_ = static_cast<uint16_t>(loadSeg + imageParas + word(10) + 0x10);

    // BIOS data area: LPT1 = 0x378, no LPT2-4, 640 KiB.
    cpu.write16(0x408, 0x0378);
    cpu.write16(0x410, 0x4021);
    cpu.write16(0x413, 640);

    // Environment, then the program path used for argv[0].
    std::string env = "COMSPEC=C:\\COMMAND.COM";
    env.push_back('\0');
    env += "PATH=C:\\";
    env.push_back('\0');
    env.push_back('\0');
    env.push_back('\x01');
    env.push_back('\0');
    env += "C:\\" + programName;
    env.push_back('\0');
    for (std::size_t i = 0; i < env.size(); ++i)
        cpu.write8(Cpu8086::linear(kEnvSeg, static_cast<uint16_t>(i)), static_cast<uint8_t>(env[i]));

    // PSP.
    const uint32_t psp = Cpu8086::linear(psp_, 0);
    cpu.write8(psp, 0xCD);
    cpu.write8(psp + 1, 0x20);
    cpu.write16(psp + 2, memTop_);
    cpu.write16(psp + 0x2C, kEnvSeg);
    const std::string tail = commandTail.empty() ? "" : " " + commandTail;
    cpu.write8(psp + 0x80, static_cast<uint8_t>(tail.size()));
    for (std::size_t i = 0; i < tail.size(); ++i)
        cpu.write8(psp + 0x81 + static_cast<uint32_t>(i), static_cast<uint8_t>(tail[i]));
    cpu.write8(psp + 0x81 + static_cast<uint32_t>(tail.size()), 0x0D);
    dta_ = psp + 0x80;

    cpu.s[Cpu8086::CS] = static_cast<uint16_t>(loadSeg + cs);
    cpu.ip = ip;
    cpu.s[Cpu8086::SS] = static_cast<uint16_t>(loadSeg + ss);
    cpu.r[Cpu8086::SP] = sp;
    cpu.s[Cpu8086::DS] = psp_;
    cpu.s[Cpu8086::ES] = psp_;
    cpu.r[Cpu8086::AX] = 0;
}

int DosMachine::run(uint64_t maxSteps)
{
    while (exitCode_ < 0) {
        if (cpu.halted)
            throw CpuError("CPU halted at " + cpu.where());
        if (cpu.steps >= maxSteps)
            throw CpuError("step limit reached at " + cpu.where());
        cpu.step();
    }
    return exitCode_;
}

void DosMachine::ok()
{
    cpu.setFlag(Cpu8086::CF, false);
}

void DosMachine::fail(uint16_t code)
{
    cpu.r[Cpu8086::AX] = code;
    cpu.setFlag(Cpu8086::CF, true);
}

int DosMachine::stdinChar()
{
    if (stdinPos_ >= stdinText.size())
        return -1;
    return static_cast<unsigned char>(stdinText[stdinPos_++]);
}

void DosMachine::fillDta(const std::string& name)
{
    for (uint32_t i = 0; i < 43; ++i)
        cpu.write8(dta_ + i, 0);
    cpu.write8(dta_ + 0x15, 0x20);
    const uint32_t size = static_cast<uint32_t>(files[name].size());
    cpu.write16(dta_ + 0x1A, static_cast<uint16_t>(size));
    cpu.write16(dta_ + 0x1C, static_cast<uint16_t>(size >> 16));
    for (std::size_t i = 0; i < name.size() && i < 12; ++i)
        cpu.write8(dta_ + 0x1E + static_cast<uint32_t>(i), static_cast<uint8_t>(name[i]));
}

bool DosMachine::handleInterrupt(uint8_t number)
{
    auto& r = cpu.r;
    switch (number) {
    case 0x20:
        exitCode_ = 0;
        return true;
    case 0x21:
        dos();
        return true;
    case 0x10:
        video();
        return true;
    case 0x16:
        keyboard();
        return true;
    case 0x1A:
        if ((r[Cpu8086::AX] >> 8) == 0) {
            ++ticks_;
            r[Cpu8086::CX] = static_cast<uint16_t>(ticks_ >> 16);
            r[Cpu8086::DX] = static_cast<uint16_t>(ticks_);
            cpu.setReg8(0, 0);
        }
        return true;
    case 0x11:
        r[Cpu8086::AX] = 0x4021;
        return true;
    case 0x12:
        r[Cpu8086::AX] = 640;
        return true;
    case 0x2F:
    case 0x33:
        r[Cpu8086::AX] &= 0xFF00;
        return true;
    default:
        return false; // through the vector table (e.g. Borland's INT 0)
    }
}

void DosMachine::video()
{
    auto& r = cpu.r;
    const uint8_t ah = static_cast<uint8_t>(r[Cpu8086::AX] >> 8);
    switch (ah) {
    case 0x0F: r[Cpu8086::AX] = 0x5003; r[Cpu8086::BX] &= 0x00FF; break;
    case 0x03: r[Cpu8086::DX] = 0; r[Cpu8086::CX] = 0x0607; break;
    case 0x08: r[Cpu8086::AX] = 0x0720; break;
    case 0x0E: console.push_back(static_cast<char>(cpu.reg8(0))); break;
    case 0x09:
    case 0x0A:
        console.append(r[Cpu8086::CX], static_cast<char>(cpu.reg8(0)));
        break;
    case 0x1A: cpu.setReg8(0, 0); break; // no VGA
    default: break; // mode set, cursor, scrolling: nothing to emulate
    }
}

void DosMachine::keyboard()
{
    auto& r = cpu.r;
    const uint8_t ah = static_cast<uint8_t>(r[Cpu8086::AX] >> 8);
    if (ah == 0x00 || ah == 0x10) {
        const int c = stdinChar();
        r[Cpu8086::AX] = static_cast<uint16_t>(c < 0 ? 0x011B : c);
    } else if (ah == 0x01 || ah == 0x11) {
        cpu.setFlag(Cpu8086::ZF, true);
    } else {
        cpu.setReg8(0, 0);
    }
}

void DosMachine::dos()
{
    auto& r = cpu.r;
    auto& s = cpu.s;
    const uint8_t ah = static_cast<uint8_t>(r[Cpu8086::AX] >> 8);
    const uint8_t al = cpu.reg8(0);
    const uint16_t bx = r[Cpu8086::BX];
    const uint16_t cx = r[Cpu8086::CX];
    const uint16_t dx = r[Cpu8086::DX];
    const uint16_t ds = s[Cpu8086::DS];

    switch (ah) {
    case 0x00: exitCode_ = 0; return;
    case 0x01:
    case 0x07:
    case 0x08: {
        const int c = stdinChar();
        cpu.setReg8(0, static_cast<uint8_t>(c < 0 ? 0x1B : c));
        if (ah == 0x01)
            console.push_back(static_cast<char>(cpu.reg8(0)));
        return;
    }
    case 0x02: console.push_back(static_cast<char>(dx)); return;
    case 0x06:
        if ((dx & 0xFF) == 0xFF) {
            cpu.setFlag(Cpu8086::ZF, true);
            cpu.setReg8(0, 0);
        } else {
            console.push_back(static_cast<char>(dx));
        }
        return;
    case 0x09:
        for (uint16_t i = 0;; ++i) {
            const char c = static_cast<char>(cpu.read8(Cpu8086::linear(ds, static_cast<uint16_t>(dx + i))));
            if (c == '$')
                break;
            console.push_back(c);
        }
        return;
    case 0x0B: cpu.setReg8(0, 0); return; // no key pressed
    case 0x0E: cpu.setReg8(0, 26); return;
    case 0x19: cpu.setReg8(0, 2); return; // C:
    case 0x1A: dta_ = Cpu8086::linear(ds, dx); return;
    case 0x25: cpu.write16(al * 4u, dx); cpu.write16(al * 4u + 2, ds); return;
    case 0x2A: r[Cpu8086::CX] = 1998; r[Cpu8086::DX] = 0x0206; cpu.setReg8(0, 5); return;
    case 0x2C: r[Cpu8086::CX] = 0; r[Cpu8086::DX] = 0; return;
    case 0x2F:
        s[Cpu8086::ES] = static_cast<uint16_t>(dta_ >> 4);
        r[Cpu8086::BX] = static_cast<uint16_t>(dta_ & 0x0F);
        return;
    case 0x30: r[Cpu8086::AX] = 0x0005; r[Cpu8086::BX] = 0; r[Cpu8086::CX] = 0; return;
    case 0x33: r[Cpu8086::DX] &= 0xFF00; return;
    case 0x35:
        r[Cpu8086::BX] = cpu.read16(al * 4u);
        s[Cpu8086::ES] = cpu.read16(al * 4u + 2);
        return;
    case 0x3B: ok(); return;
    case 0x3C:
    case 0x3D: {
        const std::string name = normalise(readString(ds, dx));
        if (ah == 0x3D && !files.count(name)) {
            fail(2);
            return;
        }
        if (ah == 0x3C)
            files[name].clear();
        int handle = 5;
        while (handles_.count(handle))
            ++handle;
        handles_[handle] = Handle { name, 0 };
        r[Cpu8086::AX] = static_cast<uint16_t>(handle);
        ok();
        return;
    }
    case 0x3E: handles_.erase(bx); ok(); return;
    case 0x3F: {
        const uint32_t buf = Cpu8086::linear(ds, dx);
        uint16_t count = 0;
        if (bx == 0) {
            // Cooked console input: one line, CR LF terminated.
            while (count < cx) {
                const int c = stdinChar();
                if (c < 0)
                    break;
                if (c == '\n') {
                    cpu.write8(buf + count++, '\r');
                    if (count < cx)
                        cpu.write8(buf + count++, '\n');
                    break;
                }
                cpu.write8(buf + count++, static_cast<uint8_t>(c));
            }
        } else if (handles_.count(bx)) {
            Handle& h = handles_[bx];
            const auto& data = files[h.name];
            while (count < cx && h.pos < data.size())
                cpu.write8(buf + count++, data[h.pos++]);
        } else {
            fail(6);
            return;
        }
        r[Cpu8086::AX] = count;
        ok();
        return;
    }
    case 0x40: {
        const uint32_t buf = Cpu8086::linear(ds, dx);
        if (bx == 1 || bx == 2) {
            for (uint16_t i = 0; i < cx; ++i)
                console.push_back(static_cast<char>(cpu.read8(buf + i)));
        } else if (handles_.count(bx)) {
            Handle& h = handles_[bx];
            auto& data = files[h.name];
            if (cx == 0)
                data.resize(h.pos);
            for (uint16_t i = 0; i < cx; ++i) {
                if (h.pos >= data.size())
                    data.resize(h.pos + 1);
                data[h.pos++] = cpu.read8(buf + i);
            }
        } else if (bx > 4) {
            fail(6);
            return;
        }
        r[Cpu8086::AX] = cx;
        ok();
        return;
    }
    case 0x41: files.erase(normalise(readString(ds, dx))); ok(); return;
    case 0x42: {
        if (!handles_.count(bx)) {
            r[Cpu8086::AX] = 0;
            r[Cpu8086::DX] = 0;
            ok();
            return;
        }
        Handle& h = handles_[bx];
        const int32_t offset = static_cast<int32_t>((static_cast<uint32_t>(cx) << 16) | dx);
        int64_t base = 0;
        if (al == 1)
            base = static_cast<int64_t>(h.pos);
        else if (al == 2)
            base = static_cast<int64_t>(files[h.name].size());
        const int64_t pos = std::max<int64_t>(0, base + offset);
        h.pos = static_cast<std::size_t>(pos);
        r[Cpu8086::AX] = static_cast<uint16_t>(pos);
        r[Cpu8086::DX] = static_cast<uint16_t>(pos >> 16);
        ok();
        return;
    }
    case 0x43: {
        const std::string name = normalise(readString(ds, dx));
        if (!files.count(name)) {
            fail(2);
            return;
        }
        r[Cpu8086::CX] = 0x20;
        ok();
        return;
    }
    case 0x44:
        if (al == 0x00) {
            r[Cpu8086::DX] = bx <= 4 ? 0x80D3 : 0x0002;
            ok();
        } else if (al == 0x01) {
            ok();
        } else {
            fail(1);
        }
        return;
    case 0x47: cpu.write8(Cpu8086::linear(ds, r[Cpu8086::SI]), 0); ok(); return;
    case 0x48:
        if (static_cast<uint32_t>(nextAlloc_) + bx <= memTop_) {
            r[Cpu8086::AX] = nextAlloc_;
            nextAlloc_ = static_cast<uint16_t>(nextAlloc_ + bx);
            ok();
        } else {
            r[Cpu8086::BX] = static_cast<uint16_t>(memTop_ - nextAlloc_);
            fail(8);
        }
        return;
    case 0x49: ok(); return;
    case 0x4A: {
        const uint16_t es = s[Cpu8086::ES];
        const uint16_t available = static_cast<uint16_t>(memTop_ - es);
        if (bx > available) {
            r[Cpu8086::BX] = available;
            fail(8);
            return;
        }
        if (es == psp_)
            nextAlloc_ = std::max<uint16_t>(nextAlloc_, static_cast<uint16_t>(es + bx));
        ok();
        return;
    }
    case 0x4C: exitCode_ = al; return;
    case 0x4E:
    case 0x4F: {
        if (ah == 0x4E) {
            const std::string pattern = normalise(readString(ds, dx));
            findList_.clear();
            for (const auto& [name, data] : files) {
                (void)data;
                if (wildcardMatch(pattern, name))
                    findList_.push_back(name);
            }
            findIndex_ = 0;
        }
        if (findIndex_ >= findList_.size()) {
            fail(18);
            return;
        }
        fillDta(findList_[findIndex_++]);
        r[Cpu8086::AX] = 0;
        ok();
        return;
    }
    case 0x58: r[Cpu8086::AX] = 0; ok(); return;
    case 0x67: ok(); return;
    default: {
        char buf[64];
        std::snprintf(buf, sizeof buf, "unsupported INT 21h AH=%02Xh", ah);
        throw CpuError(std::string(buf) + " at " + cpu.where());
    }
    }
}

} // namespace dostrace
