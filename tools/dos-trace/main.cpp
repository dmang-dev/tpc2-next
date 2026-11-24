// SPDX-License-Identifier: MIT
//
// Runs the original DOS tools (TPC2.EXE, V64IO2.EXE) on an 8086 interpreter
// whose printer port is wired to the pin-level V64 model from the tests,
// then repeats the same operations with ParallelLink against a fresh copy of
// the model and diffs the two pin traces. The DOS binaries are not part of
// this repository; pass their paths on the command line.
//
//   tpc2_dos_trace --tpc2 TPC2.EXE --v64io2 V64IO2.EXE --out DIR [--only NAME]
//
// DIR receives one .dos.trace, .next.trace and .diff file per scenario plus
// report.md. Traces of the original tools are derived from their code, so do
// not commit or redistribute them.

#include "DosMachine.h"

#include "V64Sim.h"
#include "core/Session.h"
#include "link/ParallelLink.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using tpc2::test::FakeV64Port;
using tpc2::test::V64Event;
using tpc2::test::V64Sim;

namespace {

std::vector<uint8_t> readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path.string());
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

void writeFile(const fs::path& path, const std::vector<uint8_t>& data)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

void writeLines(const fs::path& path, const std::vector<std::string>& lines)
{
    std::ofstream out(path, std::ios::binary);
    for (const auto& line : lines)
        out << line << '\n';
}

std::vector<uint8_t> pattern(std::size_t size, unsigned seed)
{
    std::vector<uint8_t> data(size);
    uint32_t x = seed * 2654435761u + 1;
    for (auto& b : data) {
        x = x * 1103515245u + 12345u;
        b = static_cast<uint8_t>(x >> 16);
    }
    return data;
}

const char* eventName(v64_event_kind kind)
{
    switch (kind) {
    case V64_EV_SYNC: return "SYNC";
    case V64_EV_ABORT: return "ABORT";
    case V64_EV_BAD_HEADER: return "BAD_HEADER";
    case V64_EV_UPLOAD_BEGIN: return "UPLOAD_BEGIN";
    case V64_EV_UPLOAD_END: return "UPLOAD_END";
    case V64_EV_DOWNLOAD_BEGIN: return "DOWNLOAD_BEGIN";
    case V64_EV_DOWNLOAD_END: return "DOWNLOAD_END";
    case V64_EV_MEM_READ: return "MEM_READ";
    case V64_EV_MEM_WRITE: return "MEM_WRITE";
    case V64_EV_CALL: return "CALL";
    case V64_EV_OSD: return "OSD";
    case V64_EV_REJECT: return "REJECT";
    }
    return "?";
}

std::vector<std::string> eventLines(const std::vector<V64Event>& events)
{
    std::vector<std::string> lines;
    for (const auto& e : events) {
        std::ostringstream s;
        char head[64];
        std::snprintf(head, sizeof head, "%s a=%08X b=%08X", eventName(e.kind), e.a, e.b);
        s << head;
        if (!e.bytes.empty()) {
            s << " [";
            const std::size_t shown = std::min<std::size_t>(e.bytes.size(), 24);
            for (std::size_t i = 0; i < shown; ++i) {
                char hex[4];
                std::snprintf(hex, sizeof hex, "%02X", e.bytes[i]);
                s << (i ? " " : "") << hex;
            }
            if (shown < e.bytes.size())
                s << " ... " << e.bytes.size() << " bytes";
            s << "]";
        }
        lines.push_back(s.str());
    }
    return lines;
}

// ---------------------------------------------------------------- diff

struct Edit {
    char op; // ' ', '-', '+'
    std::size_t a;
    std::size_t b;
};

struct DiffResult {
    std::size_t prefix = 0;  // identical leading lines
    std::size_t suffix = 0;  // identical trailing lines
    long distance = -1;      // insertions + deletions, -1 when over the cap
    std::vector<Edit> script;
};

// Myers O(ND) diff on the lines between the common prefix and suffix.
DiffResult diffLines(const std::vector<std::string>& a, const std::vector<std::string>& b,
                     long maxD = 3000)
{
    DiffResult r;
    while (r.prefix < a.size() && r.prefix < b.size() && a[r.prefix] == b[r.prefix])
        ++r.prefix;
    while (r.suffix < a.size() - r.prefix && r.suffix < b.size() - r.prefix
           && a[a.size() - 1 - r.suffix] == b[b.size() - 1 - r.suffix])
        ++r.suffix;
    const long n = static_cast<long>(a.size() - r.prefix - r.suffix);
    const long m = static_cast<long>(b.size() - r.prefix - r.suffix);
    auto A = [&](long i) -> const std::string& { return a[r.prefix + static_cast<std::size_t>(i)]; };
    auto B = [&](long j) -> const std::string& { return b[r.prefix + static_cast<std::size_t>(j)]; };

    const long maxEdit = std::min(n + m, maxD);
    std::vector<std::vector<long>> trace;
    std::vector<long> v(static_cast<std::size_t>(2 * maxEdit + 3), 0);
    const long off = maxEdit + 1;
    long found = -1;
    for (long d = 0; d <= maxEdit && found < 0; ++d) {
        for (long k = -d; k <= d; k += 2) {
            long x;
            if (k == -d || (k != d && v[static_cast<std::size_t>(off + k - 1)]
                                          < v[static_cast<std::size_t>(off + k + 1)]))
                x = v[static_cast<std::size_t>(off + k + 1)];
            else
                x = v[static_cast<std::size_t>(off + k - 1)] + 1;
            long y = x - k;
            while (x < n && y < m && A(x) == B(y)) {
                ++x;
                ++y;
            }
            v[static_cast<std::size_t>(off + k)] = x;
            if (x >= n && y >= m) {
                found = d;
                break;
            }
        }
        trace.emplace_back(v.begin() + (off - d - 1), v.begin() + (off + d + 2));
    }
    if (found < 0)
        return r;
    r.distance = found;

    // Backtrack.
    long x = n;
    long y = m;
    std::vector<Edit> rev;
    for (long d = found; d > 0; --d) {
        const auto& pv = trace[static_cast<std::size_t>(d - 1)];
        auto at = [&](long k) { return pv[static_cast<std::size_t>(k + d)]; }; // window -(d)..d
        const long k = x - y;
        long prevK;
        if (k == -d || (k != d && at(k - 1) < at(k + 1)))
            prevK = k + 1;
        else
            prevK = k - 1;
        const long prevX = at(prevK);
        const long prevY = prevX - prevK;
        while (x > prevX && y > prevY) {
            --x;
            --y;
            rev.push_back({ ' ', static_cast<std::size_t>(x), static_cast<std::size_t>(y) });
        }
        if (x == prevX)
            rev.push_back({ '+', static_cast<std::size_t>(x), static_cast<std::size_t>(--y) });
        else
            rev.push_back({ '-', static_cast<std::size_t>(--x), static_cast<std::size_t>(y) });
    }
    while (x > 0 && y > 0) {
        --x;
        --y;
        rev.push_back({ ' ', static_cast<std::size_t>(x), static_cast<std::size_t>(y) });
    }
    r.script.assign(rev.rbegin(), rev.rend());
    for (auto& e : r.script) {
        e.a += r.prefix;
        e.b += r.prefix;
    }
    return r;
}

std::vector<std::string> renderDiff(const std::vector<std::string>& a,
                                    const std::vector<std::string>& b, const DiffResult& d,
                                    std::size_t context = 3)
{
    std::vector<std::string> out;
    if (d.distance < 0) {
        out.push_back("(more than the edit cap apart; first divergence at line "
                      + std::to_string(d.prefix + 1) + ")");
        for (std::size_t i = d.prefix; i < std::min(a.size(), d.prefix + 20); ++i)
            out.push_back("-" + a[i]);
        for (std::size_t i = d.prefix; i < std::min(b.size(), d.prefix + 20); ++i)
            out.push_back("+" + b[i]);
        return out;
    }
    const auto& s = d.script;
    std::vector<bool> keep(s.size(), false);
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i].op == ' ')
            continue;
        const std::size_t lo = i >= context ? i - context : 0;
        const std::size_t hi = std::min(s.size(), i + context + 1);
        for (std::size_t j = lo; j < hi; ++j)
            keep[j] = true;
    }
    bool gap = true;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (!keep[i]) {
            gap = true;
            continue;
        }
        if (gap)
            out.push_back("@@ dos line " + std::to_string(s[i].a + 1) + ", next line "
                          + std::to_string(s[i].b + 1) + " @@");
        gap = false;
        if (s[i].op == '+')
            out.push_back("+" + b[s[i].b]);
        else
            out.push_back(std::string(1, s[i].op) + a[s[i].a]);
    }
    return out;
}

// ---------------------------------------------------------------- runs

struct RunResult {
    std::string error;
    int exitCode = 0;
    uint64_t steps = 0;
    std::string console;
    std::vector<std::string> trace;
    std::vector<std::string> timedTrace;
    std::vector<std::string> events;
    std::map<std::string, std::vector<uint8_t>> files; // files the run produced
    std::vector<uint8_t> dram;                        // first bytes of DRAM
    uint64_t virtualUs = 0;
};

struct Scenario {
    std::string name;
    std::string description;
    uint8_t modes = V64_HS_OLD | V64_HS_NEW;
    std::function<void(V64Sim&)> prepare;
    // DOS side.
    bool v64io2 = false;
    std::string commandTail;
    std::string stdinText;
    std::map<std::string, std::vector<uint8_t>> inputs;
    // ParallelLink side.
    std::function<void(tpc2::Session&, const fs::path& work, RunResult&)> next;
};

void finish(V64Sim& sim, RunResult& r, std::size_t dramBytes)
{
    r.trace = sim.traceLines(false);
    r.timedTrace = sim.traceLines(true);
    r.events = eventLines(sim.events());
    r.dram.assign(sim.dram().begin(),
                  sim.dram().begin() + static_cast<std::ptrdiff_t>(std::min(dramBytes, sim.dram().size())));
    r.virtualUs = sim.nowUs();
}

RunResult runDos(const Scenario& sc, const std::vector<uint8_t>& exe, std::size_t dramBytes)
{
    RunResult r;
    V64Sim sim;
    sim.config().modes = sc.modes;
    sim.setDelay(3);
    if (sc.prepare)
        sc.prepare(sim);
    sim.setTracing(true);

    FakeV64Port port(sim);
    port.open(0x378);
    uint8_t dataLatch = 0;
    std::vector<std::string> strayPorts;

    dostrace::DosMachine dos;
    dos.files = sc.inputs;
    dos.stdinText = sc.stdinText;
    dos.portOut = [&](uint16_t p, uint8_t v) {
        if (p == 0x378) {
            dataLatch = v;
            port.writeData(v);
        } else if (p == 0x37A) {
            port.writeControl(v);
        } else if (strayPorts.size() < 16) {
            strayPorts.push_back("out " + std::to_string(p));
        }
    };
    dos.portIn = [&](uint16_t p) -> uint8_t {
        if (p == 0x378) {
            sim.tick(1);
            return dataLatch;
        }
        if (p == 0x379)
            return port.readStatus();
        if (p == 0x37A)
            return port.readControl();
        if (strayPorts.size() < 16)
            strayPorts.push_back("in " + std::to_string(p));
        return 0xFF;
    };

    try {
        dos.load(exe, sc.v64io2 ? "V64IO2.EXE" : "TPC2.EXE", sc.commandTail);
        r.exitCode = dos.run(400'000'000ull);
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    r.steps = dos.cpu.steps;
    r.console = dos.console;
    for (const auto& s : strayPorts)
        r.console += "\n[stray port access: " + s + "]";
    for (const auto& [name, data] : dos.files)
        if (!sc.inputs.count(name) || sc.inputs.at(name) != data)
            r.files[name] = data;
    finish(sim, r, dramBytes);
    return r;
}

RunResult runNext(const Scenario& sc, const fs::path& work, std::size_t dramBytes)
{
    RunResult r;
    V64Sim sim;
    sim.config().modes = sc.modes;
    sim.setDelay(3);
    if (sc.prepare)
        sc.prepare(sim);
    sim.setTracing(true);

    fs::remove_all(work);
    fs::create_directories(work);
    for (const auto& [name, data] : sc.inputs)
        writeFile(work / name, data);

    auto port = std::make_unique<FakeV64Port>(sim);
    tpc2::ParallelLink link(std::move(port), 0x378);
    link.setClock([&sim] { return sim.now(); }, [] {});
    try {
        // Like the DOS tools: open the port, then let each operation sync.
        link.open();
        tpc2::Session session(link);
        sc.next(session, work, r);
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    link.close();
    for (const auto& entry : fs::directory_iterator(work)) {
        const std::string name = entry.path().filename().string();
        const auto data = readFile(entry.path());
        if (!sc.inputs.count(name) || sc.inputs.at(name) != data)
            r.files[name] = data;
    }
    finish(sim, r, dramBytes);
    return r;
}

std::string summarizeBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    if (a == b)
        return "identical (" + std::to_string(a.size()) + " bytes)";
    std::size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i])
        ++i;
    return "differ: sizes " + std::to_string(a.size()) + " / " + std::to_string(b.size())
        + ", first difference at offset " + std::to_string(i);
}

std::string lastLines(const std::string& text, std::size_t count)
{
    std::vector<std::string> lines;
    std::string line;
    for (char c : text) {
        if (c == '\r')
            continue;
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
        } else if (static_cast<unsigned char>(c) >= 0x20 || c == '\t') {
            line.push_back(c);
        } else {
            char hex[8];
            std::snprintf(hex, sizeof hex, "\\x%02X", static_cast<unsigned char>(c));
            line += hex;
        }
    }
    if (!line.empty())
        lines.push_back(line);
    const std::size_t from = lines.size() > count ? lines.size() - count : 0;
    std::string out;
    for (std::size_t i = from; i < lines.size(); ++i)
        out += "    " + lines[i] + "\n";
    return out;
}

std::vector<Scenario> scenarios()
{
    std::vector<Scenario> list;
    const auto game = pattern(40000, 1); // crosses one 32 KiB block boundary
    const auto saved = pattern(5000, 2);
    const auto data = pattern(0x80, 3);

    auto upload = [&](const char* name, uint8_t modes) {
        Scenario s;
        s.name = name;
        s.description = "TPC2 1 GAME.BIN (40000 bytes)";
        s.modes = modes;
        s.commandTail = "1 GAME.BIN";
        s.inputs["GAME.BIN"] = game;
        s.next = [](tpc2::Session& session, const fs::path& work, RunResult&) {
            session.upload({ { work / "GAME.BIN", "" } });
        };
        list.push_back(s);
    };
    upload("upload-old", V64_HS_OLD);
    upload("upload-new", V64_HS_NEW);

    auto download = [&](const char* name, uint8_t modes) {
        Scenario s;
        s.name = name;
        s.description = "TPC2 1 /OUT.BIN with one 5000-byte file SAVE.BIN on the V64";
        s.modes = modes;
        s.commandTail = "1 /OUT.BIN";
        s.prepare = [saved](V64Sim& sim) { sim.addFile("SAVE.BIN", saved); };
        s.next = [](tpc2::Session& session, const fs::path& work, RunResult&) {
            session.download(work);
        };
        list.push_back(s);
    };
    download("download-old", V64_HS_OLD);
    download("download-new", V64_HS_NEW);

    {
        Scenario s;
        s.name = "download-names";
        s.description = "TPC2 1 /OUT.BIN with files \"MY GAME.V64\" and \"A.B\" on the V64";
        s.modes = V64_HS_OLD;
        s.commandTail = "1 /OUT.BIN";
        const auto small = pattern(300, 4);
        s.prepare = [saved, small](V64Sim& sim) {
            sim.addFile("MY GAME.V64", saved);
            sim.addFile("A.B", small);
        };
        s.next = [](tpc2::Session& session, const fs::path& work, RunResult&) {
            session.download(work);
        };
        list.push_back(s);
    }
    {
        Scenario s;
        s.name = "download-empty";
        s.description = "TPC2 1 /OUT.BIN with no files on the V64";
        s.modes = V64_HS_OLD;
        s.commandTail = "1 /OUT.BIN";
        s.next = [](tpc2::Session& session, const fs::path& work, RunResult&) {
            session.download(work);
        };
        list.push_back(s);
    }

    auto io = [&](const char* name, uint8_t modes) {
        Scenario s;
        s.name = name;
        s.description = "V64IO2 script: load, write DRAM, read DRAM, save, write CPU, call, print";
        s.modes = modes;
        s.v64io2 = true;
        s.stdinText = "load DATA.BIN\n"
                      "write 2000 40\n"
                      "read 2000 40\n"
                      "save 40 RB.BIN\n"
                      "write 80000300 10\n"
                      "call 1234\n"
                      "print 2 3 HELLO V64\n"
                      "quit\n";
        s.inputs["DATA.BIN"] = data;
        s.next = [data](tpc2::Session& session, const fs::path& work, RunResult&) {
            session.writeMemory(0x2000, std::span(data).first(0x40));
            const auto back = session.readMemory(0x2000, 0x40);
            writeFile(work / "RB.BIN", back);
            session.writeMemory(0x80000300u, std::span(data).first(0x10));
            session.call(0x1234);
            session.printOsd(2, 3, "HELLO V64");
        };
        list.push_back(s);
    };
    io("v64io2-old", V64_HS_OLD);
    io("v64io2-new", V64_HS_NEW);
    return list;
}

} // namespace

int main(int argc, char** argv)
{
    fs::path tpc2Exe;
    fs::path v64io2Exe;
    fs::path outDir = "dos-trace-out";
    std::string only;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        if (key == "--tpc2")
            tpc2Exe = argv[i + 1];
        else if (key == "--v64io2")
            v64io2Exe = argv[i + 1];
        else if (key == "--out")
            outDir = argv[i + 1];
        else if (key == "--only")
            only = argv[i + 1];
    }
    if (tpc2Exe.empty() && v64io2Exe.empty()) {
        std::cerr << "usage: tpc2_dos_trace --tpc2 TPC2.EXE --v64io2 V64IO2.EXE --out DIR"
                     " [--only NAME]\n";
        return 2;
    }
    fs::create_directories(outDir);

    std::ostringstream report;
    report << "# DOS trace comparison\n\n"
           << "Pin traces of the original DOS tools against ParallelLink, both driving the\n"
           << "same V64 model (tests/v64). Generated by tools/dos-trace; do not commit.\n\n";
    int problems = 0;

    for (const auto& sc : scenarios()) {
        if (!only.empty() && sc.name != only)
            continue;
        const fs::path& exePath = sc.v64io2 ? v64io2Exe : tpc2Exe;
        if (exePath.empty())
            continue;
        std::cerr << "== " << sc.name << '\n';
        const auto exe = readFile(exePath);
        const std::size_t dramBytes = 0x10000;
        RunResult dos = runDos(sc, exe, dramBytes);
        RunResult next = runNext(sc, outDir / (sc.name + ".work"), dramBytes);

        writeLines(outDir / (sc.name + ".dos.trace"), dos.timedTrace);
        writeLines(outDir / (sc.name + ".next.trace"), next.timedTrace);
        const DiffResult d = diffLines(dos.trace, next.trace);
        writeLines(outDir / (sc.name + ".diff"), renderDiff(dos.trace, next.trace, d));
        const DiffResult ev = diffLines(dos.events, next.events);
        const auto evDiff = renderDiff(dos.events, next.events, ev, 1);

        report << "## " << sc.name << "\n\n" << sc.description << ", V64 accepts "
               << ((sc.modes & V64_HS_OLD) ? "old" : "") << ((sc.modes == 3) ? "+" : "")
               << ((sc.modes & V64_HS_NEW) ? "new" : "") << " handshake.\n\n";
        report << "| | DOS original | ParallelLink |\n|---|---|---|\n";
        report << "| result | " << (dos.error.empty() ? "exit " + std::to_string(dos.exitCode) : dos.error)
               << " | " << (next.error.empty() ? "ok" : next.error) << " |\n";
        report << "| trace lines | " << dos.trace.size() << " | " << next.trace.size() << " |\n";
        report << "| virtual time | " << dos.virtualUs << " us | " << next.virtualUs << " us |\n";
        report << "| V64 events | " << dos.events.size() << " | " << next.events.size() << " |\n";
        report << "| 8086 instructions | " << dos.steps << " | |\n\n";
        report << "Trace: common prefix " << d.prefix << " lines, common suffix " << d.suffix
               << " lines, edit distance "
               << (d.distance < 0 ? std::string("over the cap") : std::to_string(d.distance))
               << " (see " << sc.name << ".diff).\n\n";
        report << "Events " << (ev.distance == 0 ? "identical" : "differ") << ":\n\n```\n";
        if (ev.distance == 0) {
            for (const auto& line : dos.events)
                report << " " << line << "\n";
        } else {
            for (const auto& line : evDiff)
                report << line << "\n";
        }
        report << "```\n\n";
        report << "DRAM 0x0000-0xFFFF after the run: " << summarizeBytes(dos.dram, next.dram) << "\n\n";
        std::vector<std::string> names;
        for (const auto& [n, data] : dos.files)
            names.push_back(n);
        for (const auto& [n, data] : next.files)
            if (!dos.files.count(n))
                names.push_back(n);
        for (const auto& n : names) {
            const auto a = dos.files.count(n) ? dos.files.at(n) : std::vector<uint8_t>();
            const auto b = next.files.count(n) ? next.files.at(n) : std::vector<uint8_t>();
            report << "File " << n << ": " << (dos.files.count(n) ? "" : "(only ParallelLink) ")
                   << (next.files.count(n) ? "" : "(only DOS) ") << summarizeBytes(a, b) << "\n\n";
        }
        report << "DOS console (last lines):\n\n```\n" << lastLines(dos.console, 12) << "```\n\n";
        if (!dos.error.empty() || !next.error.empty())
            ++problems;
        std::cerr << "   dos: " << (dos.error.empty() ? "ok" : dos.error) << ", " << dos.trace.size()
                  << " lines; next: " << (next.error.empty() ? "ok" : next.error) << ", "
                  << next.trace.size() << " lines; distance "
                  << d.distance << "\n";
    }

    std::ofstream(outDir / "report.md", std::ios::binary) << report.str();
    std::cerr << "report: " << (outDir / "report.md").string() << '\n';
    return problems ? 1 : 0;
}
