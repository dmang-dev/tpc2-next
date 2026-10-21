// SPDX-License-Identifier: MIT
#include "core/Session.h"

#include "core/Error.h"
#include "core/Protocol.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <system_error>
#include <thread>

namespace fs = std::filesystem;

namespace tpc2 {
namespace {

constexpr uint64_t kMaxIncomingFile = 256ull * 1024 * 1024;
constexpr uint8_t kAckRead = 0xA0;
constexpr uint8_t kAckWrite = 0xA1;
constexpr uint8_t kAckCall = 0xA2;
constexpr uint8_t kAckPrint = 0xA3;

std::string fileSizeError(const fs::path& path)
{
    return "File is too large for the 32-bit TPC protocol: " + path.string();
}

} // namespace

Session::Session(Link& link)
    : link_(link)
{
}

DeviceInfo Session::connect()
{
    if (!link_.isOpen())
        link_.open();
    const ComMode mode = link_.sync();
    return DeviceInfo { link_.description(), mode };
}

void Session::checkCancelled(const CancelFn& cancelled)
{
    if (cancelled && cancelled())
        throw Error(ErrorCode::Cancelled, "Operation cancelled");
}

std::vector<std::string> Session::upload(const std::vector<UploadItem>& items,
                                         const ProgressFn& progress,
                                         const CancelFn& cancelled)
{
    if (items.empty())
        throw Error(ErrorCode::InvalidArgument, "No files selected");

    uint64_t total = 0;
    std::vector<uint64_t> sizes;
    sizes.reserve(items.size());
    for (const auto& item : items) {
        std::error_code ec;
        const uint64_t size = fs::file_size(item.path, ec);
        if (ec)
            throw Error(ErrorCode::Io, "Cannot read " + item.path.string()
                                           + ": " + ec.message());
        if (size > std::numeric_limits<uint32_t>::max())
            throw Error(ErrorCode::InvalidArgument, fileSizeError(item.path));
        sizes.push_back(size);
        total += size;
    }

    std::vector<std::string> sentNames;
    uint64_t done = 0;
    for (std::size_t index = 0; index < items.size(); ++index) {
        checkCancelled(cancelled);
        const auto& item = items[index];

        std::string name = item.remoteName;
        if (name.empty())
            name = item.path.filename().string();
        const auto encodedName = proto::encodeName(name);
        const std::string actualName =
            proto::decodeName(encodedName.data(), encodedName.size());

        std::ifstream file(item.path, std::ios::binary);
        if (!file)
            throw Error(ErrorCode::Io, "Cannot open " + item.path.string());

        link_.sync();

        // GD6R + single byte file counter. The original TPC always sends 0.
        std::array<uint8_t, 5> header { 'G', 'D', '6', 'R', 0 };
        link_.write(header);

        std::vector<uint8_t> meta;
        meta.reserve(15);
        proto::appendLe32(meta, static_cast<uint32_t>(sizes[index]));
        meta.insert(meta.end(), encodedName.begin(), encodedName.end());
        link_.write(meta);

        std::array<uint8_t, proto::kChunkSize> buffer {};
        uint64_t remaining = sizes[index];
        while (remaining > 0) {
            checkCancelled(cancelled);
            const std::size_t want = static_cast<std::size_t>(
                std::min<uint64_t>(remaining, buffer.size()));
            file.read(reinterpret_cast<char*>(buffer.data()),
                      static_cast<std::streamsize>(want));
            const auto got = static_cast<std::size_t>(file.gcount());
            if (got != want)
                throw Error(ErrorCode::Io, "Short read from " + item.path.string());
            link_.write(std::span<const uint8_t>(buffer.data(), got));
            remaining -= got;
            done += got;
            if (progress)
                progress(ProgressReport { done, total });
        }
        if (progress)
            progress(ProgressReport { done, total });
        sentNames.push_back(actualName);
    }
    return sentNames;
}

std::vector<ReceivedFile> Session::download(const fs::path& destination,
                                            const ProgressFn& progress,
                                            const CancelFn& cancelled)
{
    std::error_code ec;
    fs::create_directories(destination, ec);
    if (ec)
        throw Error(ErrorCode::Io, "Cannot create " + destination.string()
                                       + ": " + ec.message());

    link_.sync();
    link_.write(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>("GD6W"), 4));
    // The original TPC2 sends an 11-byte 8.3 filename after GD6W before it
    // waits for the V64 handshake. The V64 names incoming files from the
    // headers it sends later, so a blank field is sufficient, but the bytes
    // must still be present for the device to finish parsing the command.
    std::array<uint8_t, proto::kFullNameField> requestedName {};
    requestedName.fill(' ');
    link_.write(requestedName);
    link_.checkSync();

    std::array<uint8_t, 1> countBuffer {};
    link_.read(countBuffer);
    const unsigned totalFiles = countBuffer[0];

    std::vector<ReceivedFile> received;
    uint64_t done = 0;
    for (unsigned fileIndex = 0; fileIndex < totalFiles; ++fileIndex) {
        checkCancelled(cancelled);
        std::array<uint8_t, 15> header {};
        link_.read(header);

        const uint32_t size = proto::readLe32(header.data());
        if (size > kMaxIncomingFile)
            throw Error(ErrorCode::Protocol,
                        "Device announced an implausible file size ("
                            + std::to_string(size) + " bytes)");

        const std::string rawName = proto::decodeName(header.data() + 4);
        const fs::path path = proto::uniquePath(destination, rawName);

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            throw Error(ErrorCode::Io, "Cannot create " + path.string());

        std::array<uint8_t, proto::kChunkSize> buffer {};
        uint32_t remaining = size;
        while (remaining > 0) {
            checkCancelled(cancelled);
            const auto want = static_cast<std::size_t>(
                std::min<uint32_t>(remaining, buffer.size()));
            link_.read(std::span<uint8_t>(buffer.data(), want));
            file.write(reinterpret_cast<const char*>(buffer.data()),
                       static_cast<std::streamsize>(want));
            if (!file)
                throw Error(ErrorCode::Io, "Write failed: " + path.string());
            remaining -= static_cast<uint32_t>(want);
            done += want;
            if (progress)
                progress(ProgressReport { done, 0 });
        }
        file.close();
        received.push_back(ReceivedFile { rawName, path, size });
    }
    if (progress)
        progress(ProgressReport { done, done });
    return received;
}

void Session::sendExpandHeader(uint8_t code)
{
    std::array<uint8_t, 5> header { 'G', 'D', '6', 'E', code };
    link_.write(header);
}

void Session::sendExpandArgs(uint32_t address, uint32_t length)
{
    std::vector<uint8_t> args;
    args.reserve(8);
    proto::appendBe32(args, address);
    proto::appendBe32(args, length);
    link_.write(args);
}

uint8_t Session::expectAck(uint8_t expected, const char* operation)
{
    link_.checkSync();
    std::array<uint8_t, 1> ack {};
    link_.read(ack);
    if (ack[0] != expected) {
        throw Error(ErrorCode::DeviceRejected,
                    std::string(operation) + " rejected by V64 (reply 0x"
                        + [&] {
                              char buf[8];
                              std::snprintf(buf, sizeof(buf), "%02X", ack[0]);
                              return std::string(buf);
                          }()
                        + ")");
    }
    // V64IO2 deliberately pauses after the acknowledge byte in the newer
    // handshake before streaming data. Keep a small margin for slow BIOS
    // revisions and USB bridge latency.
    if (link_.mode() == ComMode::New)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return ack[0];
}

std::vector<uint8_t> Session::readMemory(uint32_t address, uint32_t length,
                                         const ProgressFn& progress,
                                         const CancelFn& cancelled)
{
    if (length == 0)
        throw Error(ErrorCode::InvalidArgument, "Length must be greater than zero");
    if (static_cast<uint64_t>(address) + length > 0x1'0000'0000ull)
        throw Error(ErrorCode::InvalidArgument, "Address range overflow");

    link_.sync();
    sendExpandHeader('R');
    sendExpandArgs(address, length);
    expectAck(kAckRead, "Read");

    std::vector<uint8_t> out(length);
    std::size_t done = 0;
    while (done < out.size()) {
        checkCancelled(cancelled);
        const std::size_t want =
            std::min<std::size_t>(proto::kChunkSize, out.size() - done);
        link_.read(std::span<uint8_t>(out.data() + done, want));
        done += want;
        if (progress)
            progress(ProgressReport { done, out.size() });
    }
    return out;
}

void Session::writeMemory(uint32_t address, std::span<const uint8_t> data,
                          const ProgressFn& progress,
                          const CancelFn& cancelled)
{
    if (data.empty())
        throw Error(ErrorCode::InvalidArgument, "No data to write");
    if (static_cast<uint64_t>(address) + data.size() > 0x1'0000'0000ull)
        throw Error(ErrorCode::InvalidArgument, "Address range overflow");

    link_.sync();
    sendExpandHeader('W');
    sendExpandArgs(address, static_cast<uint32_t>(data.size()));
    expectAck(kAckWrite, "Write");

    std::size_t done = 0;
    while (done < data.size()) {
        checkCancelled(cancelled);
        const std::size_t chunk =
            std::min<std::size_t>(proto::kChunkSize, data.size() - done);
        link_.write(data.subspan(done, chunk));
        done += chunk;
        if (progress)
            progress(ProgressReport { done, data.size() });
    }
}

void Session::call(uint32_t address)
{
    if (address > 0xFFFF)
        throw Error(ErrorCode::InvalidArgument, "Call address must be 0..FFFF");
    link_.sync();
    sendExpandHeader('C');
    std::array<uint8_t, 2> args {
        static_cast<uint8_t>(address >> 8),
        static_cast<uint8_t>(address),
    };
    link_.write(args);
    expectAck(kAckCall, "Call");
}

void Session::printOsd(uint8_t row, uint8_t column, const std::string& text,
                       bool flash, bool necFont)
{
    if (row > 0x0A)
        throw Error(ErrorCode::InvalidArgument, "OSD row must be 0..10");
    if (column > 0x17)
        throw Error(ErrorCode::InvalidArgument, "OSD column must be 0..23");
    if (text.size() > 255)
        throw Error(ErrorCode::InvalidArgument, "OSD text is limited to 255 bytes");

    std::vector<uint8_t> payload;
    payload.reserve(text.size() + 1);
    for (unsigned char c : text) {
        if (c == 0xFF)
            throw Error(ErrorCode::InvalidArgument,
                        "0xFF is the OSD string terminator and cannot be displayed");
        if (c < 0x20 || c > 0x7E)
            throw Error(ErrorCode::InvalidArgument,
                        "OSD text must be printable ASCII (0x20-0x7E)");
        uint8_t value = c;
        if (flash)
            value |= 0x80;
        if (necFont)
            value |= 0x40;
        payload.push_back(value);
    }
    payload.push_back(0xFF);

    link_.sync();
    sendExpandHeader('P');
    std::array<uint8_t, 2> args { row, column };
    link_.write(args);
    expectAck(kAckPrint, "Print");
    link_.write(payload);
}

} // namespace tpc2
