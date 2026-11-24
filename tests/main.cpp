// SPDX-License-Identifier: MIT
#include "core/Error.h"
#include "core/Protocol.h"
#include "core/Session.h"
#include "io/BridgeFrame.h"
#include "link/BridgeLink.h"
#include "link/DemoLink.h"
#include "link/ParallelLink.h"

#include "FakePicoTransport.h"
#include "V64Sim.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace tpc2;

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        ++checks;                                                               \
        if (!(condition)) {                                                     \
            ++failures;                                                         \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << "  "         \
                      << #condition << '\n';                                    \
        }                                                                       \
    } while (0)

#define CHECK_EQ(actual, expected)                                              \
    do {                                                                        \
        ++checks;                                                               \
        const auto& a_ = (actual);                                              \
        const auto& e_ = (expected);                                            \
        if (!(a_ == e_)) {                                                      \
            ++failures;                                                         \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << "  "         \
                      << #actual << " == " << #expected << '\n';                \
        }                                                                       \
    } while (0)

template <typename Fn>
void checkThrows(Fn&& fn, ErrorCode code, const char* what, int line)
{
    ++checks;
    try {
        fn();
        ++failures;
        std::cerr << "FAIL " << __FILE__ << ':' << line << "  expected "
                  << what << " to throw\n";
    } catch (const Error& error) {
        if (error.code() != code) {
            ++failures;
            std::cerr << "FAIL " << __FILE__ << ':' << line << "  " << what
                      << " threw " << errorCodeName(error.code()) << ", expected "
                      << errorCodeName(code) << " (" << error.what() << ")\n";
        }
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAIL " << __FILE__ << ':' << line << "  " << what
                  << " threw unexpected std::exception: " << error.what() << '\n';
    }
}

#define CHECK_THROWS_CODE(expr, code) checkThrows([&] { expr; }, code, #expr, __LINE__)

class TempDir {
public:
    TempDir()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("tpc2-tests-" + std::to_string(stamp));
        fs::create_directories(path_);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

std::vector<uint8_t> makePattern(std::size_t size, uint32_t seed)
{
    std::vector<uint8_t> data(size);
    uint32_t value = seed;
    for (auto& byte : data) {
        value = value * 1664525u + 1013904223u;
        byte = static_cast<uint8_t>(value >> 24);
    }
    return data;
}

void writeFile(const fs::path& path, const std::vector<uint8_t>& data)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    CHECK(static_cast<bool>(out));
}

std::vector<uint8_t> readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

void testNames()
{
    const auto basic = proto::encodeName("hello.txt");
    CHECK_EQ(proto::decodeName(basic.data()), std::string("HELLO.TXT"));

    const auto longName = proto::encodeName("averylongfilename.bin");
    CHECK_EQ(proto::decodeName(longName.data()), std::string("AVERYLON.BIN"));

    const auto noExt = proto::encodeName("README");
    CHECK_EQ(proto::decodeName(noExt.data()), std::string("README"));

    const auto spaced = proto::encodeName("my file.bin");
    CHECK_EQ(proto::decodeName(spaced.data()), std::string("MY_FILE.BIN"));

    CHECK_EQ(proto::sanitizeLocalName("../../evil.bin"), std::string("evil.bin"));
    CHECK_EQ(proto::sanitizeLocalName("CON"), std::string("_CON"));
    CHECK_EQ(proto::sanitizeLocalName("a<b>c?.z64"), std::string("a_b_c_.z64"));

    CHECK(proto::isBiosImageName("V64_VER1.75"));
    CHECK(proto::isBiosImageName("v64_ver2.01"));
    CHECK(!proto::isBiosImageName("V64_VER1.7"));
    CHECK(!proto::isBiosImageName("OTHER.BIN"));

    CHECK_THROWS_CODE(proto::encodeName("dir/file.bin"), ErrorCode::InvalidArgument);
    CHECK_THROWS_CODE(proto::encodeName(""), ErrorCode::InvalidArgument);

    TempDir dir;
    fs::create_directories(dir.path() / "sub");
    writeFile(dir.path() / "NAME.BIN", { 1, 2, 3 });
    const fs::path unique = proto::uniquePath(dir.path(), "NAME.BIN");
    CHECK_EQ(unique.filename().string(), std::string("NAME (1).BIN"));
}

void testCrcAndFrames()
{
    const char* text = "123456789";
    const uint16_t crc = bridge::crc16(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(text), 9));
    CHECK_EQ(crc, static_cast<uint16_t>(0x29B1));

    const std::vector<uint8_t> payload { 0xC0, 0xDB, 0x00, 0xFF, 0xC0, 0xDB };
    bridge::Frame frame;
    frame.command = static_cast<uint8_t>(bridge::Command::Write);
    frame.sequence = 42;
    frame.data = payload;

    const auto wire = bridge::encode(frame);
    CHECK(std::find(wire.begin(), wire.end(), bridge::kSlipEnd) != wire.end());

    bridge::Decoder decoder;
    const auto decoded = decoder.feed(wire);
    CHECK_EQ(decoded.size(), std::size_t(1));
    if (decoded.size() == 1) {
        CHECK_EQ(decoded[0].command, frame.command);
        CHECK_EQ(decoded[0].sequence, frame.sequence);
        CHECK(decoded[0].data == payload);
    }

    // Corrupting the CRC must be detected instead of delivering bad data.
    auto broken = wire;
    broken[broken.size() - 2] ^= 0xFF;
    bridge::Decoder brokenDecoder;
    CHECK_THROWS_CODE(brokenDecoder.feed(broken), ErrorCode::Checksum);

    // Feeding a frame one byte at a time must work as well.
    bridge::Decoder slowDecoder;
    std::size_t frames = 0;
    for (uint8_t byte : wire) {
        const auto one = slowDecoder.feed(std::span<const uint8_t>(&byte, 1));
        frames += one.size();
    }
    CHECK_EQ(frames, std::size_t(1));
}

void testUploadDownload()
{
    TempDir dir;
    const auto original = makePattern(100000, 0x12345678);
    const fs::path source = dir.path() / "source.bin";
    writeFile(source, original);

    DemoLink demo(false);
    Session session(demo);
    const DeviceInfo info = session.connect();
    CHECK_EQ(info.mode, ComMode::Old);

    uint64_t lastDone = 0;
    uint64_t lastTotal = 0;
    const auto names = session.upload(
        { UploadItem { source, "TEST.BIN" } },
        [&](const ProgressReport& progress) {
            lastDone = progress.done;
            lastTotal = progress.total;
        });
    CHECK_EQ(names.size(), std::size_t(1));
    if (!names.empty())
        CHECK_EQ(names[0], std::string("TEST.BIN"));
    CHECK_EQ(lastDone, static_cast<uint64_t>(original.size()));
    CHECK_EQ(lastTotal, static_cast<uint64_t>(original.size()));

    CHECK_EQ(demo.files().size(), std::size_t(1));
    if (!demo.files().empty()) {
        CHECK_EQ(demo.files()[0].name, std::string("TEST.BIN"));
        CHECK(demo.files()[0].data == original);
    }

    const fs::path outDir = dir.path() / "dump";
    const auto received = session.download(outDir);
    CHECK_EQ(received.size(), std::size_t(1));
    if (!received.empty()) {
        CHECK_EQ(received[0].name, std::string("TEST.BIN"));
        CHECK_EQ(received[0].size, static_cast<uint64_t>(original.size()));
        CHECK(readFile(received[0].path) == original);
    }

    // Two files with the same remote name must not overwrite each other.
    demo.clearFiles();
    session.upload({ UploadItem { source, "DUP.BIN" }, UploadItem { source, "DUP.BIN" } });
    const fs::path outDir2 = dir.path() / "dump2";
    const auto received2 = session.download(outDir2);
    CHECK_EQ(received2.size(), std::size_t(2));
    if (received2.size() == 2) {
        CHECK_EQ(received2[0].path.filename().string(), std::string("DUP.BIN"));
        CHECK_EQ(received2[1].path.filename().string(), std::string("DUP (1).BIN"));
        CHECK(fs::exists(received2[0].path));
        CHECK(fs::exists(received2[1].path));
    }
}

void testExtendedCommands()
{
    DemoLink demo(false);
    Session session(demo);
    session.connect();

    const auto data = makePattern(512, 7);
    session.writeMemory(0x001000, data);
    const auto readBack = session.readMemory(0x001000, 512);
    CHECK(readBack == data);

    const auto cpuData = makePattern(64, 9);
    session.writeMemory(0x80000200, cpuData);
    const auto cpuRead = session.readMemory(0x80000200, 64);
    CHECK(cpuRead == cpuData);

    session.call(0xFFE3);
    CHECK_EQ(demo.lastCall(), uint32_t(0xFFE3));

    session.printOsd(2, 4, "HELLO V64");
    CHECK_EQ(demo.lastOsd(), std::string("HELLO V64"));

    // V64IO2 D7/D6 flags are applied to every character.
    session.printOsd(0, 0, "AB", true, false);
    CHECK_EQ(demo.lastOsd(), std::string("\xC1\xC2"));
    session.printOsd(0, 0, " !", false, true);
    CHECK_EQ(demo.lastOsd(), std::string("\x60\x61"));
    session.printOsd(0, 0, "A", true, true);
    CHECK_EQ(demo.lastOsd(), std::string("\xC1"));

    CHECK_THROWS_CODE(session.call(0x1FFFF), ErrorCode::InvalidArgument);
    CHECK_THROWS_CODE(session.printOsd(11, 0, "x"), ErrorCode::InvalidArgument);
    CHECK_THROWS_CODE(session.printOsd(0, 0, std::string(1, '\xFF')),
                      ErrorCode::InvalidArgument);
    CHECK_THROWS_CODE(session.printOsd(0, 0, "\xC3\xA9"),
                      ErrorCode::InvalidArgument);
    CHECK_THROWS_CODE(session.readMemory(0x7FFFFF, 2), ErrorCode::InvalidArgument);
}

void testCancellation()
{
    TempDir dir;
    const auto data = makePattern(70000, 11);
    const fs::path source = dir.path() / "cancel.bin";
    writeFile(source, data);

    DemoLink demo(false);
    Session session(demo);
    session.connect();

    bool cancelled = false;
    try {
        session.upload({ UploadItem { source, "CANCEL.BIN" } }, {},
                       [&] { return true; });
    } catch (const Error& error) {
        cancelled = error.code() == ErrorCode::Cancelled;
    }
    CHECK(cancelled);
}

void testBiosName()
{
    // The BIOS updater is picky about the name, so make sure the encoder keeps
    // the exact 8.3 form used by Bung.
    const auto encoded = proto::encodeName("V64_VER1.75");
    CHECK_EQ(proto::decodeName(encoded.data()), std::string("V64_VER1.75"));
}

// ---------------------------------------------------------------------------
// Pin-level V64 tests. The real ParallelLink and the real Pico firmware run
// against the simulated V64 in tests/v64 (see v64_model.h for the rules and
// where each one comes from).
// ---------------------------------------------------------------------------

enum class Backend { Parallel, Bridge };

const char* backendName(Backend backend)
{
    return backend == Backend::Parallel ? "parallel" : "bridge";
}

const char* modeName(ComMode mode)
{
    return mode == ComMode::New ? "new" : "old";
}

std::vector<uint8_t> nameField(const std::string& name)
{
    const auto field = test::rawName(name);
    return std::vector<uint8_t>(field.begin(), field.end());
}

// A link of the requested kind wired to a fresh simulated V64. ParallelLink
// runs on the simulator's virtual clock (1 us per register access); the
// firmware's own timers use the same clock through the GPIO shim.
struct Rig {
    explicit Rig(Backend backend, uint8_t modes = V64_HS_OLD | V64_HS_NEW,
                 uint32_t delayUs = 3)
    {
        sim.config().modes = modes;
        sim.setDelay(delayUs);
        if (backend == Backend::Parallel) {
            auto port = std::make_unique<test::FakeV64Port>(sim);
            auto parallel = std::make_unique<ParallelLink>(std::move(port), 0x378);
            parallel->setClock([this] { return sim.now(); }, [] {});
            // Virtual time: far above the V64's reaction time, short enough
            // that the failure tests finish quickly.
            parallel->setTimeouts(std::chrono::milliseconds(100),
                                  std::chrono::milliseconds(20));
            link = std::move(parallel);
        } else {
            auto transport = std::make_unique<test::FakePicoTransport>(sim);
            pico = transport.get();
            link = std::make_unique<BridgeLink>(std::move(transport));
        }
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    ~Rig()
    {
        if (link)
            link->close();
    }

    test::V64Sim sim;
    std::unique_ptr<Link> link;
    test::FakePicoTransport* pico = nullptr;
};

template <typename Fn>
void runTest(const std::string& name, Fn&& fn)
{
    std::cerr << name << '\n';
    try {
        fn();
    } catch (const std::exception& error) {
        ++checks;
        ++failures;
        std::cerr << "FAIL " << name << " threw: " << error.what() << '\n';
    }
}

std::ptrdiff_t findLine(const std::vector<std::string>& lines, std::size_t from,
                        std::initializer_list<const char*> parts)
{
    for (std::size_t i = from; i < lines.size(); ++i) {
        bool all = true;
        for (const char* part : parts)
            all = all && lines[i].find(part) != std::string::npos;
        if (all)
            return static_cast<std::ptrdiff_t>(i);
    }
    return -1;
}

void testV64Sync(Backend backend)
{
    struct Case {
        uint8_t modes;
        std::optional<ComMode> preferred;
        bool ok;
        ComMode expected;
    };
    const Case cases[] = {
        { V64_HS_OLD | V64_HS_NEW, std::nullopt, true, ComMode::Old },
        { V64_HS_NEW, std::nullopt, true, ComMode::New }, // auto falls back
        { V64_HS_OLD, std::nullopt, true, ComMode::Old },
        { V64_HS_OLD | V64_HS_NEW, ComMode::New, true, ComMode::New },
        { V64_HS_NEW, ComMode::Old, false, ComMode::Old },
        { V64_HS_OLD, ComMode::New, false, ComMode::Old },
    };
    for (const auto& c : cases) {
        Rig rig(backend, c.modes);
        rig.link->setPreferredMode(c.preferred);
        Session session(*rig.link);
        if (c.ok) {
            CHECK_EQ(session.connect().mode, c.expected);
            CHECK_EQ(rig.link->mode(), c.expected);
            const auto syncs = rig.sim.eventsOf(V64_EV_SYNC);
            CHECK_EQ(syncs.size(), std::size_t(1));
            if (!syncs.empty())
                CHECK_EQ(syncs[0].a, uint32_t(c.expected == ComMode::New ? V64_HS_NEW
                                                                         : V64_HS_OLD));
        } else {
            CHECK_THROWS_CODE(session.connect(), ErrorCode::Timeout);
            CHECK(rig.sim.eventsOf(V64_EV_SYNC).empty());
        }
    }
}

void testV64SyncRemembersMode(Backend backend)
{
    // Auto mode starts with the old handshake, but once the new one has
    // worked, later syncs try it first. Otherwise every operation against a
    // new-only V64 waits out the old-mode timeout. The DOS tools keep the
    // working mode too [T].
    Rig rig(backend, V64_HS_NEW);
    Session session(*rig.link);
    CHECK_EQ(session.connect().mode, ComMode::New);
    const uint64_t first = rig.sim.nowUs();
    CHECK(first >= 50'000); // the failed old attempt timed out once

    rig.sim.setTracing(true);
    for (int i = 0; i < 3; ++i)
        CHECK_EQ(session.connect().mode, ComMode::New);
    CHECK(rig.sim.nowUs() - first < 100'000);
    CHECK(findLine(rig.sim.traceLines(), 0, { "D=AA" }) < 0);
    CHECK_EQ(rig.sim.eventsOf(V64_EV_SYNC).size(), std::size_t(4));

    // A unit swapped for an old-only one: the remembered mode fails and the
    // link falls back to the other handshake.
    rig.sim.config().modes = V64_HS_OLD;
    rig.sim.reset();
    CHECK_EQ(session.connect().mode, ComMode::Old);
    CHECK_EQ(rig.link->mode(), ComMode::Old);
    // And old is now tried first again.
    const uint64_t before = rig.sim.nowUs();
    CHECK_EQ(session.connect().mode, ComMode::Old);
    CHECK(rig.sim.nowUs() - before < 100'000);
}

void testV64SyncTrace(Backend backend)
{
    // The pulses on the wire, as seen by the V64 [PROTOCOL.md 1].
    for (ComMode mode : { ComMode::Old, ComMode::New }) {
        Rig rig(backend);
        rig.link->setPreferredMode(mode);
        rig.sim.setTracing(true);
        Session session(*rig.link);
        session.connect();
        const auto lines = rig.sim.traceLines();
        const char* ack = mode == ComMode::Old ? "SI=0" : "ER=0";
        const auto first = findLine(
            lines, 0, { mode == ComMode::Old ? "D=AA" : "D=A5", "INI=0", ack });
        CHECK(first >= 0);
        const auto second = findLine(
            lines, static_cast<std::size_t>(std::max<std::ptrdiff_t>(first, 0)),
            { mode == ComMode::Old ? "D=55" : "D=5A", "INI=0", ack });
        CHECK(second > first);
    }
}

void testV64Upload(Backend backend, ComMode mode)
{
    TempDir dir;
    Rig rig(backend);
    rig.link->setPreferredMode(mode);
    Session session(*rig.link);
    CHECK_EQ(session.connect().mode, mode);

    // Odd and even sizes, and one larger than both the 32 KiB session chunk
    // and the 8 KiB bridge frame, so the STROBE phase must carry over.
    for (std::size_t size : { std::size_t(0), std::size_t(1), std::size_t(1001),
                              std::size_t(70001) }) {
        const auto data = makePattern(size, static_cast<uint32_t>(size) + 3);
        const fs::path file = dir.path() / ("up" + std::to_string(size) + ".bin");
        writeFile(file, data);
        rig.sim.clearEvents();
        const auto names = session.upload({ UploadItem { file, "game.z64" } });
        CHECK_EQ(names.size(), std::size_t(1));
        const auto begin = rig.sim.eventsOf(V64_EV_UPLOAD_BEGIN);
        const auto end = rig.sim.eventsOf(V64_EV_UPLOAD_END);
        CHECK_EQ(begin.size(), std::size_t(1));
        CHECK_EQ(end.size(), std::size_t(1));
        if (begin.size() == 1 && end.size() == 1) {
            CHECK_EQ(begin[0].a, static_cast<uint32_t>(size)); // size_le32
            CHECK_EQ(begin[0].b, uint32_t(0));                 // file counter
            CHECK(begin[0].bytes == nameField("GAME.Z64"));
            CHECK_EQ(end[0].a, static_cast<uint32_t>(size));
        }
        CHECK(std::equal(data.begin(), data.end(), rig.sim.dram().begin()));
    }

    // Several files: one sync and one GD6R per file [PROTOCOL.md 2].
    const fs::path a = dir.path() / "a.bin";
    const fs::path b = dir.path() / "b.bin";
    writeFile(a, makePattern(33, 1));
    writeFile(b, makePattern(34, 2));
    rig.sim.clearEvents();
    session.upload({ UploadItem { a, "" }, UploadItem { b, "V64_VER1.75" } });
    CHECK_EQ(rig.sim.eventsOf(V64_EV_SYNC).size(), std::size_t(2));
    const auto begins = rig.sim.eventsOf(V64_EV_UPLOAD_BEGIN);
    CHECK_EQ(begins.size(), std::size_t(2));
    if (begins.size() == 2) {
        CHECK(begins[0].bytes == nameField("A.BIN"));
        CHECK(begins[1].bytes == nameField("V64_VER1.75"));
    }
}

void testV64Download(Backend backend, ComMode mode)
{
    TempDir dir;
    Rig rig(backend);
    rig.link->setPreferredMode(mode);
    Session session(*rig.link);
    session.connect();

    const auto one = makePattern(3, 1);
    const auto two = makePattern(40000, 2);
    rig.sim.addFile("ONE.BIN", one);
    rig.sim.addFile("TWO.Z64", two);
    rig.sim.addDramFile("DRAM.BIN", 0x100, 5000); // V64 pushes part of its DRAM
    const auto received = session.download(dir.path() / "in");
    CHECK_EQ(received.size(), std::size_t(3));
    if (received.size() == 3) {
        CHECK_EQ(received[0].name, std::string("ONE.BIN"));
        CHECK_EQ(received[1].name, std::string("TWO.Z64"));
        CHECK_EQ(received[2].name, std::string("DRAM.BIN"));
        CHECK(readFile(received[0].path) == one);
        CHECK(readFile(received[1].path) == two);
        const std::vector<uint8_t> dram(rig.sim.dram().begin() + 0x100,
                                        rig.sim.dram().begin() + 0x100 + 5000);
        CHECK(readFile(received[2].path) == dram);
    }
    const auto begin = rig.sim.eventsOf(V64_EV_DOWNLOAD_BEGIN);
    CHECK_EQ(begin.size(), std::size_t(1));
    if (!begin.empty()) {
        CHECK_EQ(begin[0].a, uint32_t(3));
        CHECK(begin[0].bytes == std::vector<uint8_t>(11, ' ')); // blank request field
    }

    // Round trip: upload an image, then let the V64 push its DRAM back.
    const auto image = makePattern(12345, 77);
    const fs::path source = dir.path() / "image.bin";
    writeFile(source, image);
    session.upload({ UploadItem { source, "IMAGE.BIN" } });
    rig.sim.clearFiles();
    rig.sim.addDramFile("IMAGE.BIN", 0, static_cast<uint32_t>(image.size()));
    const auto back = session.download(dir.path() / "back");
    CHECK_EQ(back.size(), std::size_t(1));
    if (back.size() == 1)
        CHECK(readFile(back[0].path) == image);

    // Nothing selected on the V64: a zero count and no files.
    rig.sim.clearFiles();
    CHECK(session.download(dir.path() / "empty").empty());
}

void testV64Memory(Backend backend, ComMode mode)
{
    Rig rig(backend);
    rig.link->setPreferredMode(mode);
    Session session(*rig.link);
    session.connect();

    const auto data = makePattern(600, 5);
    session.writeMemory(0x1000, data);
    CHECK(std::equal(data.begin(), data.end(), rig.sim.dram().begin() + 0x1000));
    CHECK(session.readMemory(0x1000, 600) == data);

    const auto big = session.readMemory(0x20000, 40000);
    CHECK(std::equal(big.begin(), big.end(), rig.sim.dram().begin() + 0x20000));

    const auto cpuData = makePattern(64, 6);
    session.writeMemory(0x80000200, cpuData);
    CHECK(std::equal(cpuData.begin(), cpuData.end(), rig.sim.cpu().begin() + 0x200));
    CHECK(session.readMemory(0x80000200, 64) == cpuData);
    const auto edge = session.readMemory(0x8000BFF0, 16);
    CHECK(std::equal(edge.begin(), edge.end(), rig.sim.cpu().begin() + 0xBFF0));

    const auto reads = rig.sim.eventsOf(V64_EV_MEM_READ);
    CHECK(!reads.empty());
    if (!reads.empty()) {
        CHECK_EQ(reads[0].a, uint32_t(0x1000)); // address_be32 decoded
        CHECK_EQ(reads[0].b, uint32_t(600));    // length_be32 is a byte count
    }

    // Region rules are enforced by the V64, which answers with a non-ACK
    // byte [PROTOCOL.md 4].
    CHECK_THROWS_CODE(session.writeMemory(0x80000100, cpuData), ErrorCode::DeviceRejected);
    CHECK_THROWS_CODE(session.writeMemory(0x800007F0, cpuData), ErrorCode::DeviceRejected);
    CHECK_THROWS_CODE(session.readMemory(0x8000C000, 1), ErrorCode::DeviceRejected);
    CHECK_THROWS_CODE(session.readMemory(0x8000BFFF, 2), ErrorCode::DeviceRejected);
    CHECK_THROWS_CODE(session.readMemory(
                          static_cast<uint32_t>(rig.sim.dram().size() - 1), 2),
                      ErrorCode::DeviceRejected);
    CHECK_EQ(rig.sim.eventsOf(V64_EV_REJECT).size(), std::size_t(5));

    // The link stays usable after a rejection.
    const auto again = session.readMemory(0x1000, 4);
    CHECK(std::equal(again.begin(), again.end(), data.begin()));
}

void testV64CallAndOsd(Backend backend, ComMode mode)
{
    Rig rig(backend);
    rig.link->setPreferredMode(mode);
    Session session(*rig.link);
    session.connect();

    session.call(0xFFE3);
    session.call(0x0000);
    const auto calls = rig.sim.eventsOf(V64_EV_CALL);
    CHECK_EQ(calls.size(), std::size_t(2));
    if (calls.size() == 2) {
        CHECK_EQ(calls[0].a, uint32_t(0xFFE3)); // address_be16
        CHECK_EQ(calls[1].a, uint32_t(0));
    }

    session.printOsd(2, 4, "HELLO V64");
    session.printOsd(10, 23, "AB", true, true);
    session.printOsd(0, 0, "");
    const auto osd = rig.sim.eventsOf(V64_EV_OSD);
    CHECK_EQ(osd.size(), std::size_t(3));
    if (osd.size() == 3) {
        CHECK_EQ(osd[0].a, uint32_t(2));
        CHECK_EQ(osd[0].b, uint32_t(4));
        CHECK(osd[0].bytes == std::vector<uint8_t>({ 'H', 'E', 'L', 'L', 'O', ' ', 'V', '6', '4' }));
        CHECK_EQ(osd[1].a, uint32_t(10));
        CHECK_EQ(osd[1].b, uint32_t(23));
        CHECK(osd[1].bytes == std::vector<uint8_t>({ 0xC1, 0xC2 })); // D7 flash, D6 NEC
        CHECK(osd[2].bytes.empty());
    }
}

void testV64Cancellation(Backend backend)
{
    TempDir dir;
    Rig rig(backend);
    Session session(*rig.link);
    session.connect();

    // Cancel an upload after its first 32 KiB chunk: the V64 is left
    // mid-transfer and the next sync must restart it.
    const auto big = makePattern(100000, 9);
    const fs::path source = dir.path() / "big.bin";
    writeFile(source, big);
    int polls = 0;
    CHECK_THROWS_CODE(session.upload({ UploadItem { source, "BIG.BIN" } }, {},
                                     [&] { return ++polls > 2; }),
                      ErrorCode::Cancelled);
    CHECK(rig.sim.eventsOf(V64_EV_UPLOAD_END).empty());

    const auto small = makePattern(77, 10);
    const fs::path smallFile = dir.path() / "small.bin";
    writeFile(smallFile, small);
    session.upload({ UploadItem { smallFile, "SMALL.BIN" } });
    CHECK_EQ(rig.sim.eventsOf(V64_EV_ABORT).size(), std::size_t(1));
    CHECK_EQ(rig.sim.eventsOf(V64_EV_UPLOAD_END).size(), std::size_t(1));
    CHECK(std::equal(small.begin(), small.end(), rig.sim.dram().begin()));

    // Cancel a download in the middle of the V64's reply.
    rig.sim.clearEvents();
    rig.sim.addFile("HUGE.BIN", makePattern(100000, 11));
    polls = 0;
    CHECK_THROWS_CODE(session.download(dir.path() / "cancelled", {},
                                       [&] { return ++polls > 2; }),
                      ErrorCode::Cancelled);
    CHECK(rig.sim.eventsOf(V64_EV_DOWNLOAD_END).empty());
    session.call(0x1234);
    CHECK_EQ(rig.sim.eventsOf(V64_EV_ABORT).size(), std::size_t(1));
    CHECK_EQ(rig.sim.eventsOf(V64_EV_CALL).size(), std::size_t(1));

    // Cancel a memory read part way.
    polls = 0;
    CHECK_THROWS_CODE(session.readMemory(0, 70000, {}, [&] { return ++polls > 1; }),
                      ErrorCode::Cancelled);
    CHECK_EQ(session.readMemory(0x10, 4).size(), std::size_t(4));
}

void testV64Errors(Backend backend)
{
    TempDir dir;
    {
        // Nothing answers: V64 switched off or not on the transfer screen.
        Rig rig(backend);
        rig.sim.config().powered = false;
        Session session(*rig.link);
        CHECK_THROWS_CODE(session.connect(), ErrorCode::Timeout);
    }
    {
        // Wrong acknowledge byte.
        Rig rig(backend);
        Session session(*rig.link);
        session.connect();
        rig.sim.config().ack_override = 0x5A;
        CHECK_THROWS_CODE(session.call(0x1000), ErrorCode::DeviceRejected);
        CHECK_THROWS_CODE(session.readMemory(0, 16), ErrorCode::DeviceRejected);
        CHECK_THROWS_CODE(session.printOsd(0, 0, "X"), ErrorCode::DeviceRejected);
        rig.sim.config().ack_override = -1;
        session.call(0x1000);
        CHECK_EQ(rig.sim.eventsOf(V64_EV_CALL).size(), std::size_t(2));
    }
    {
        // A V64 that rejects without replying: checkSync times out.
        Rig rig(backend);
        Session session(*rig.link);
        session.connect();
        rig.sim.config().reject_reply = V64_REPLY_SILENT;
        CHECK_THROWS_CODE(session.readMemory(0x8000C000, 4), ErrorCode::Timeout);
        session.call(0x2000);
        CHECK_EQ(rig.sim.eventsOf(V64_EV_CALL).size(), std::size_t(1));
    }
    {
        // BIOS older than 1.30: GD6E is not understood [PROTOCOL.md 4].
        Rig rig(backend);
        rig.sim.config().extended = false;
        Session session(*rig.link);
        session.connect();
        CHECK_THROWS_CODE(session.call(0x1000), ErrorCode::Timeout);
        CHECK_EQ(rig.sim.eventsOf(V64_EV_BAD_HEADER).size(), std::size_t(1));
        const fs::path file = dir.path() / "ok.bin";
        writeFile(file, makePattern(10, 1));
        session.upload({ UploadItem { file, "OK.BIN" } });
        CHECK_EQ(rig.sim.eventsOf(V64_EV_UPLOAD_END).size(), std::size_t(1));
    }
    {
        // The V64 stops accepting bytes part way through an upload.
        Rig rig(backend);
        Session session(*rig.link);
        session.connect();
        const fs::path file = dir.path() / "stall.bin";
        writeFile(file, makePattern(2000, 2));
        rig.sim.config().rx_stall_after = 500;
        CHECK_THROWS_CODE(session.upload({ UploadItem { file, "STALL.BIN" } }),
                          ErrorCode::Timeout);
        rig.sim.config().rx_stall_after = 0;
        session.upload({ UploadItem { file, "STALL.BIN" } });
        CHECK_EQ(rig.sim.eventsOf(V64_EV_UPLOAD_END).size(), std::size_t(1));
    }
    {
        // The V64 stops sending part way through a download.
        Rig rig(backend);
        Session session(*rig.link);
        session.connect();
        rig.sim.addFile("PART.BIN", makePattern(1000, 3));
        rig.sim.config().tx_stall_after = 100;
        CHECK_THROWS_CODE(session.download(dir.path() / "stall"), ErrorCode::Timeout);
        rig.sim.config().tx_stall_after = 0;
        CHECK_EQ(session.download(dir.path() / "ok").size(), std::size_t(1));
    }
    {
        // Every command is preceded by a sync; a V64 that has finished a
        // command does not accept bytes without one.
        Rig rig(backend);
        Session session(*rig.link);
        session.connect();
        session.call(0x10);
        const std::array<uint8_t, 4> header { 'G', 'D', '6', 'E' };
        CHECK_THROWS_CODE(rig.link->write(header), ErrorCode::Timeout);
    }
}

void testV64Latency()
{
    // Instant and slow V64 reactions must give identical results.
    for (uint32_t delay : { 0u, 40u }) {
        TempDir dir;
        Rig rig(Backend::Parallel, V64_HS_OLD | V64_HS_NEW, delay);
        Session session(*rig.link);
        for (ComMode mode : { ComMode::Old, ComMode::New }) {
            rig.link->setPreferredMode(mode);
            CHECK_EQ(session.connect().mode, mode);
            const auto data = makePattern(301, delay + 1);
            const fs::path file = dir.path() / "lat.bin";
            writeFile(file, data);
            session.upload({ UploadItem { file, "LAT.BIN" } });
            CHECK(std::equal(data.begin(), data.end(), rig.sim.dram().begin()));
            CHECK(session.readMemory(0, 301) == data);
        }
    }
}

void testV64Determinism()
{
    // Same inputs, same pin trace, down to the simulated microsecond.
    std::vector<std::string> first;
    for (int run = 0; run < 2; ++run) {
        Rig rig(Backend::Parallel);
        rig.sim.setTracing(true);
        Session session(*rig.link);
        session.connect();
        session.call(0xBEEF);
        session.readMemory(0x40, 8);
        const auto lines = rig.sim.traceLines(true);
        CHECK(!lines.empty());
        if (run == 0)
            first = lines;
        else
            CHECK(lines == first);
    }
}

void testBridgeFirmwareFrames()
{
    Rig rig(Backend::Bridge);
    Session session(*rig.link);
    session.connect();
    CHECK(rig.link->description().find("tpc2-pico 1.0.0") != std::string::npos);

    uint8_t sequence = 200;
    auto raw = [&](uint8_t command, std::vector<uint8_t> payload, bool corrupt = false) {
        bridge::Frame request;
        request.command = command;
        request.sequence = sequence++;
        request.data = std::move(payload);
        auto wire = bridge::encode(request);
        if (corrupt)
            wire[1] ^= 0x01;
        rig.pico->injectToFirmware(wire.data(), wire.size());
        std::vector<uint8_t> bytes(70000);
        const std::size_t got = rig.pico->read(bytes.data(), bytes.size(), 0);
        bridge::Decoder decoder;
        return decoder.feed(std::span<const uint8_t>(bytes.data(), got));
    };
    auto status = [](const std::vector<bridge::Frame>& frames) {
        return frames.size() == 1 ? static_cast<int>(frames[0].status) : -1;
    };

    const auto info = raw(static_cast<uint8_t>(bridge::Command::Info), {});
    CHECK_EQ(status(info), 0);
    if (info.size() == 1) {
        const std::string text(info[0].data.begin(), info[0].data.end());
        CHECK(text.find("mode=old") != std::string::npos);
        CHECK(info[0].response);
        // Replies echo the request's command [PROTOCOL.md 5].
        CHECK_EQ(info[0].command, static_cast<uint8_t>(bridge::Command::Info));
    }
    const auto read = static_cast<uint8_t>(bridge::Command::Read);
    const int badRequest = static_cast<int>(bridge::Status::BadRequest);
    CHECK_EQ(status(raw(read, { 0, 0, 0, 0 })), badRequest);
    CHECK_EQ(status(raw(read, { 1, 0 })), badRequest);
    CHECK_EQ(status(raw(read, { 0x01, 0x20, 0, 0 })), badRequest); // 8193 > 8 KiB
    CHECK_EQ(status(raw(static_cast<uint8_t>(bridge::Command::Sync), {})), badRequest);
    CHECK_EQ(status(raw(0x55, {})), static_cast<int>(bridge::Status::Unsupported));
    CHECK(raw(static_cast<uint8_t>(bridge::Command::Ping), {}, true).empty()); // bad CRC
    CHECK(raw(static_cast<uint8_t>(bridge::Command::Write),
              std::vector<uint8_t>(8193, 0)).empty()); // oversized frame dropped

    // The bridge keeps working after the garbage above.
    session.call(0x0042);
    CHECK_EQ(rig.sim.eventsOf(V64_EV_CALL).size(), std::size_t(1));
}

} // namespace

int main()
{
    try {
        std::cerr << "testNames\n";
        testNames();
        std::cerr << "testCrcAndFrames\n";
        testCrcAndFrames();
        std::cerr << "testUploadDownload\n";
        testUploadDownload();
        std::cerr << "testExtendedCommands\n";
        testExtendedCommands();
        std::cerr << "testCancellation\n";
        testCancellation();
        std::cerr << "testBiosName\n";
        testBiosName();
    } catch (const std::exception& error) {
        std::cerr << "FATAL: " << error.what() << '\n';
        return 1;
    }

    for (Backend backend : { Backend::Parallel, Backend::Bridge }) {
        const std::string prefix = std::string("v64/") + backendName(backend) + "/";
        runTest(prefix + "sync", [&] { testV64Sync(backend); });
        runTest(prefix + "syncRemembersMode", [&] { testV64SyncRemembersMode(backend); });
        runTest(prefix + "syncTrace", [&] { testV64SyncTrace(backend); });
        for (ComMode mode : { ComMode::Old, ComMode::New }) {
            const std::string m = std::string("/") + modeName(mode);
            runTest(prefix + "upload" + m, [&] { testV64Upload(backend, mode); });
            runTest(prefix + "download" + m, [&] { testV64Download(backend, mode); });
            runTest(prefix + "memory" + m, [&] { testV64Memory(backend, mode); });
            runTest(prefix + "callOsd" + m, [&] { testV64CallAndOsd(backend, mode); });
        }
        runTest(prefix + "cancellation", [&] { testV64Cancellation(backend); });
        runTest(prefix + "errors", [&] { testV64Errors(backend); });
    }
    runTest("v64/parallel/latency", [] { testV64Latency(); });
    runTest("v64/parallel/determinism", [] { testV64Determinism(); });
    runTest("v64/bridge/frames", [] { testBridgeFirmwareFrames(); });

    std::cout << checks << " checks, " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
