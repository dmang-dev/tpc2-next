// SPDX-License-Identifier: MIT
#include "core/Error.h"
#include "core/Protocol.h"
#include "core/Session.h"
#include "io/QtSerialTransport.h"
#include "link/BridgeLink.h"
#include "link/DemoLink.h"
#include "link/ParallelLink.h"
#include "platform/ParallelPort.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSerialPortInfo>
#include <QStringList>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace tpc2;

namespace {

struct Options {
    QString backend = "demo";
    QString port;
    int baud = 115200;
    uint16_t lpt = 0x378;
    QString mode = "auto";
    bool verbose = false;
    bool flash = false;
    bool necFont = false;

    std::unique_ptr<Link> makeLink() const
    {
        std::optional<ComMode> preferred;
        if (mode == "old")
            preferred = ComMode::Old;
        else if (mode == "new")
            preferred = ComMode::New;
        else if (mode != "auto")
            throw Error(ErrorCode::InvalidArgument, "--mode must be auto, old or new");

        std::unique_ptr<Link> link;
        if (backend == "demo") {
            link = std::make_unique<DemoLink>();
        } else if (backend == "bridge") {
            if (port.isEmpty())
                throw Error(ErrorCode::InvalidArgument,
                            "--port is required for the bridge backend");
            link = std::make_unique<BridgeLink>(
                std::make_unique<QtSerialTransport>(port, baud));
        } else if (backend == "lpt") {
            link = std::make_unique<ParallelLink>(ParallelPort::create(), lpt);
        } else {
            throw Error(ErrorCode::InvalidArgument,
                        "--backend must be demo, bridge or lpt");
        }
        link->setPreferredMode(preferred);
        return link;
    }
};

fs::path toPath(const QString& text)
{
#ifdef _WIN32
    return fs::path(text.toStdWString());
#else
    return fs::path(text.toUtf8().constData());
#endif
}

QString fromPath(const fs::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromUtf8(path.string().c_str());
#endif
}

bool parseNumber(const QString& text, uint32_t& value)
{
    bool ok = false;
    if (text.startsWith("0x", Qt::CaseInsensitive))
        value = text.mid(2).toUInt(&ok, 16);
    else
        value = text.toUInt(&ok, 10);
    return ok;
}

void printProgress(const ProgressReport& report)
{
    if (report.total > 0) {
        const double percent = 100.0 * static_cast<double>(report.done)
            / static_cast<double>(report.total);
        std::fprintf(stderr, "\r  %llu / %llu bytes (%.1f%%)   ",
                     static_cast<unsigned long long>(report.done),
                     static_cast<unsigned long long>(report.total), percent);
    } else {
        std::fprintf(stderr, "\r  %llu bytes received   ",
                     static_cast<unsigned long long>(report.done));
    }
    std::fflush(stderr);
}

void clearProgress()
{
    std::fprintf(stderr, "\r%*s\r", 60, "");
}

int usage()
{
    std::puts(
        "TPC2 Next command line " TPC2_VERSION "\n"
        "\n"
        "Usage:\n"
        "  tpc2cli [options] ports\n"
        "  tpc2cli [options] info\n"
        "  tpc2cli [options] upload <file> [file...]\n"
        "  tpc2cli [options] download <directory>\n"
        "  tpc2cli [options] read <address> <length> <output.bin>\n"
        "  tpc2cli [options] write <address> <input.bin>\n"
        "  tpc2cli [options] call <address>\n"
        "  tpc2cli [options] print <row> <column> <text>\n"
        "\n"
        "Options:\n"
        "  --backend demo|bridge|lpt   demo is the default\n"
        "  --port NAME                 serial port for the bridge, e.g. COM5\n"
        "  --baud N                    serial baud rate (default 115200)\n"
        "  --lpt 0x378                 parallel port base address\n"
        "  --mode auto|old|new         handshake mode (default auto)\n"
        "  --flash                     OSD print: flashing characters (V64IO2 D7)\n"
        "  --nec                       OSD print: NEC6450 font (V64IO2 D6)\n"
        "  --verbose                   print protocol details\n"
        "\n"
        "Addresses accept 0x-prefixed hex or decimal. The demo backend is a\n"
        "self-contained V64 simulator; it is useful for trying the CLI and GUI.\n");
    return 2;
}

void listPorts()
{
    std::puts("Serial ports (USB bridge):");
    const auto serialPorts = QSerialPortInfo::availablePorts();
    if (serialPorts.isEmpty())
        std::puts("  (none)");
    for (const auto& port : serialPorts) {
        std::printf("  %-12s %s\n", port.portName().toUtf8().constData(),
                    port.description().toUtf8().constData());
    }

    std::puts("\nParallel ports:");
    const auto parallelPorts = ParallelPort::enumerate();
    if (parallelPorts.empty())
        std::puts("  (none)");
    for (const auto& port : parallelPorts) {
        std::printf("  %-6s 0x%03X  %s%s\n", port.name.c_str(), port.base,
                    port.available ? "ready" : "unavailable",
                    port.detail.empty() ? "" : (" - " + port.detail).c_str());
    }
}

} // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments();
    args.removeFirst();

    Options options;
    QString command;
    QStringList positional;

    for (int i = 0; i < args.size(); ++i) {
        const QString& arg = args[i];
        auto takeValue = [&](QString& target) {
            if (i + 1 >= args.size())
                throw Error(ErrorCode::InvalidArgument, arg.toStdString() + " needs a value");
            target = args[++i];
        };

        if (arg == "--backend") {
            takeValue(options.backend);
        } else if (arg == "--port") {
            takeValue(options.port);
        } else if (arg == "--baud") {
            QString value;
            takeValue(value);
            bool ok = false;
            const int baud = value.toInt(&ok);
            if (!ok)
                throw Error(ErrorCode::InvalidArgument, "Invalid baud rate");
            options.baud = baud;
        } else if (arg == "--lpt") {
            QString value;
            takeValue(value);
            uint32_t parsed = 0;
            if (!parseNumber(value, parsed) || parsed > 0xFFFF)
                throw Error(ErrorCode::InvalidArgument, "Invalid LPT base address");
            options.lpt = static_cast<uint16_t>(parsed);
        } else if (arg == "--mode") {
            takeValue(options.mode);
        } else if (arg == "--verbose") {
            options.verbose = true;
        } else if (arg == "--flash") {
            options.flash = true;
        } else if (arg == "--nec") {
            options.necFont = true;
        } else if (arg == "--help" || arg == "-h") {
            return usage();
        } else if (!arg.startsWith('-') && command.isEmpty()) {
            command = arg;
        } else {
            positional.push_back(arg);
        }
    }

    if (command.isEmpty())
        return usage();

    try {
        if (command == "ports") {
            listPorts();
            return 0;
        }
        if (command == "help") {
            usage();
            return 0;
        }

        auto link = options.makeLink();
        Session session(*link);
        const DeviceInfo info = session.connect();
        std::fprintf(stderr, "%s\nmode: %s\n", info.description.c_str(),
                     comModeName(info.mode));

        if (command == "info") {
            return 0;
        }

        if (command == "upload") {
            if (positional.isEmpty())
                return usage();
            std::vector<UploadItem> items;
            for (const QString& file : positional) {
                UploadItem item;
                item.path = toPath(file);
                items.push_back(std::move(item));
            }
            const auto names = session.upload(items, printProgress);
            clearProgress();
            for (const auto& name : names)
                std::printf("sent %s\n", name.c_str());
            return 0;
        }

        if (command == "download") {
            fs::path destination = positional.isEmpty() ? fs::current_path()
                                                        : toPath(positional.front());
            const auto files = session.download(destination, printProgress);
            clearProgress();
            for (const auto& file : files) {
                std::printf("%s -> %s (%llu bytes)\n", file.name.c_str(),
                            fromPath(file.path).toUtf8().constData(),
                            static_cast<unsigned long long>(file.size));
            }
            if (files.empty())
                std::puts("The V64 sent no files.");
            return 0;
        }

        if (command == "read") {
            if (positional.size() != 3)
                return usage();
            uint32_t address = 0;
            uint32_t length = 0;
            if (!parseNumber(positional[0], address) || !parseNumber(positional[1], length))
                throw Error(ErrorCode::InvalidArgument, "Invalid address or length");
            const auto data = session.readMemory(address, length, printProgress);
            clearProgress();
            std::ofstream out(toPath(positional[2]), std::ios::binary | std::ios::trunc);
            if (!out)
                throw Error(ErrorCode::Io, "Cannot create output file");
            out.write(reinterpret_cast<const char*>(data.data()),
                      static_cast<std::streamsize>(data.size()));
            std::printf("read %zu bytes -> %s\n", data.size(),
                        positional[2].toUtf8().constData());
            return 0;
        }

        if (command == "write") {
            if (positional.size() != 2)
                return usage();
            uint32_t address = 0;
            if (!parseNumber(positional[0], address))
                throw Error(ErrorCode::InvalidArgument, "Invalid address");
            std::ifstream in(toPath(positional[1]), std::ios::binary);
            if (!in)
                throw Error(ErrorCode::Io, "Cannot open input file");
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
            session.writeMemory(address, data, printProgress);
            clearProgress();
            std::printf("wrote %zu bytes to 0x%08X\n", data.size(), address);
            return 0;
        }

        if (command == "call") {
            if (positional.size() != 1)
                return usage();
            uint32_t address = 0;
            if (!parseNumber(positional[0], address))
                throw Error(ErrorCode::InvalidArgument, "Invalid address");
            session.call(address);
            std::printf("called 0x%04X\n", address);
            return 0;
        }

        if (command == "print") {
            if (positional.size() < 3)
                return usage();
            bool okRow = false;
            bool okCol = false;
            const int row = positional[0].toInt(&okRow);
            const int col = positional[1].toInt(&okCol);
            if (!okRow || !okCol)
                throw Error(ErrorCode::InvalidArgument, "Invalid row or column");
            session.printOsd(static_cast<uint8_t>(row), static_cast<uint8_t>(col),
                             positional.mid(2).join(' ').toStdString(),
                             options.flash, options.necFont);
            std::puts("OSD text sent");
            return 0;
        }

        std::fprintf(stderr, "Unknown command: %s\n", command.toUtf8().constData());
        return usage();
    } catch (const Error& error) {
        clearProgress();
        std::fprintf(stderr, "error (%s): %s\n", errorCodeName(error.code()), error.what());
        return 1;
    } catch (const std::exception& error) {
        clearProgress();
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
