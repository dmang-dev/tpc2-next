// SPDX-License-Identifier: MIT
#include "core/Error.h"
#include "core/Protocol.h"
#include "core/Session.h"
#include "io/BridgeFrame.h"
#include "link/DemoLink.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
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

    std::cout << checks << " checks, " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
