// SPDX-License-Identifier: MIT
#include "gui/DeviceWorker.h"

#include "core/Error.h"
#include "core/Protocol.h"
#include "core/Session.h"
#include "io/QtSerialTransport.h"
#include "link/BridgeLink.h"
#include "link/DemoLink.h"
#include "link/ParallelLink.h"
#include "platform/ParallelPort.h"

#include <QByteArray>
#include <QFileInfo>

#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

namespace fs = std::filesystem;
using namespace tpc2;

namespace tpc2::gui {
namespace {

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

QString humanBytes(qint64 value)
{
    static const char* units[] = { "B", "KB", "MB", "GB" };
    double amount = static_cast<double>(value);
    int unit = 0;
    while (amount >= 1024.0 && unit < 3) {
        amount /= 1024.0;
        ++unit;
    }
    return QString::number(amount, 'f', unit == 0 ? 0 : 2) + " " + units[unit];
}

QString describeError(const Error& error)
{
    return QStringLiteral("%1 [%2]")
        .arg(QString::fromStdString(error.what()),
             QString::fromLatin1(errorCodeName(error.code())));
}

} // namespace

DeviceWorker::DeviceWorker(QObject* parent)
    : QObject(parent)
{
}

DeviceWorker::~DeviceWorker()
{
    closeSession();
}

void DeviceWorker::closeSession()
{
    if (session_)
        session_.reset();
    if (link_) {
        try {
            link_->close();
        } catch (...) {
        }
        link_.reset();
    }
}

bool DeviceWorker::ensureSession()
{
    if (!session_) {
        emit errorOccurred(QStringLiteral("Not connected to a V64 device."));
        return false;
    }
    return true;
}

void DeviceWorker::connectDevice(const BackendConfig& config)
{
    closeSession();
    emit busyChanged(true);
    cancel_.store(false);

    try {
        std::unique_ptr<Link> link;
        switch (config.kind) {
        case BackendConfig::Kind::Demo:
            link = std::make_unique<DemoLink>(true);
            break;
        case BackendConfig::Kind::Bridge:
            if (config.port.isEmpty())
                throw Error(ErrorCode::InvalidArgument,
                            "No serial port selected for the USB bridge.");
            link = std::make_unique<BridgeLink>(
                std::make_unique<QtSerialTransport>(config.port, config.baud));
            break;
        case BackendConfig::Kind::Parallel:
            link = std::make_unique<ParallelLink>(
                ParallelPort::create(), config.lptBase);
            break;
        }

        if (config.mode == 1)
            link->setPreferredMode(ComMode::Old);
        else if (config.mode == 2)
            link->setPreferredMode(ComMode::New);
        else
            link->setPreferredMode(std::nullopt);

        session_ = std::make_unique<Session>(*link);
        link_ = std::move(link);

        const DeviceInfo info = session_->connect();
        emit logMessage(QStringLiteral("Connected to %1")
                            .arg(QString::fromStdString(info.description)),
                        QStringLiteral("ok"));
        emit logMessage(QStringLiteral("Handshake: %1")
                            .arg(QString::fromLatin1(comModeName(info.mode))),
                        QStringLiteral("info"));
        emit connected(QString::fromStdString(info.description),
                       static_cast<int>(info.mode));
    } catch (const Error& error) {
        closeSession();
        emit errorOccurred(describeError(error));
        emit disconnected();
    } catch (const std::exception& error) {
        closeSession();
        emit errorOccurred(QString::fromStdString(error.what()));
        emit disconnected();
    }
    emit busyChanged(false);
}

void DeviceWorker::disconnectDevice()
{
    if (session_) {
        closeSession();
        emit logMessage(QStringLiteral("Disconnected."), QStringLiteral("info"));
        emit disconnected();
    }
}

void DeviceWorker::shutdown()
{
    cancel_.store(true);
    closeSession();
}

void DeviceWorker::runJob(const QString& label, const std::function<QString()>& job)
{
    if (!ensureSession())
        return;
    cancel_.store(false);
    emit busyChanged(true);
    emit logMessage(label, QStringLiteral("info"));
    try {
        const QString summary = job();
        if (!summary.isEmpty())
            emit operationFinished(summary);
    } catch (const Error& error) {
        if (error.code() == ErrorCode::Cancelled) {
            emit logMessage(QStringLiteral("Operation cancelled."), QStringLiteral("warn"));
            emit operationFinished(QStringLiteral("Cancelled"));
        } else {
            emit errorOccurred(describeError(error));
        }
    } catch (const std::exception& error) {
        emit errorOccurred(QString::fromStdString(error.what()));
    }
    emit busyChanged(false);
}

void DeviceWorker::upload(const QStringList& paths, const QStringList& remoteNames)
{
    if (paths.isEmpty()) {
        emit errorOccurred(QStringLiteral("No files selected."));
        return;
    }
    if (paths.size() != remoteNames.size()) {
        emit errorOccurred(QStringLiteral("Internal error: file/name count mismatch."));
        return;
    }

    runJob(QStringLiteral("Upload started"), [&] {
        std::vector<UploadItem> items;
        items.reserve(static_cast<std::size_t>(paths.size()));
        qint64 totalBytes = 0;
        for (int i = 0; i < paths.size(); ++i) {
            UploadItem item;
            item.path = toPath(paths[i]);
            item.remoteName = remoteNames[i].toStdString();
            std::error_code ec;
            const auto size = fs::file_size(item.path, ec);
            if (ec) {
                throw Error(ErrorCode::Io,
                            "Cannot read " + paths[i].toStdString());
            }
            totalBytes += static_cast<qint64>(size);
            items.push_back(std::move(item));
        }

        const auto names = session_->upload(
            items,
            [this](const ProgressReport& report) {
                emit progress(static_cast<qint64>(report.done),
                              static_cast<qint64>(report.total));
            },
            [this] { return cancel_.load(std::memory_order_relaxed); });

        for (const auto& name : names)
            emit logMessage(QStringLiteral("Sent %1").arg(QString::fromStdString(name)),
                            QStringLiteral("ok"));

        return QStringLiteral("Sent %1 file(s), %2")
            .arg(names.size())
            .arg(humanBytes(totalBytes));
    });
}

void DeviceWorker::download(const QString& destination)
{
    if (destination.isEmpty()) {
        emit errorOccurred(QStringLiteral("No destination folder selected."));
        return;
    }

    runJob(QStringLiteral("Waiting for the V64 to send its DRAM image..."), [&] {
        const auto files = session_->download(
            toPath(destination),
            [this](const ProgressReport& report) {
                emit progress(static_cast<qint64>(report.done),
                              static_cast<qint64>(report.total));
            },
            [this] { return cancel_.load(std::memory_order_relaxed); });

        qint64 total = 0;
        for (const auto& file : files) {
            total += static_cast<qint64>(file.size);
            emit logMessage(QStringLiteral("%1  ->  %2  (%3)")
                                .arg(QString::fromStdString(file.name),
                                     fromPath(file.path), humanBytes(
                                         static_cast<qint64>(file.size))),
                            QStringLiteral("ok"));
        }
        if (files.empty())
            return QStringLiteral("The V64 sent no files.");
        return QStringLiteral("Received %1 file(s), %2")
            .arg(files.size())
            .arg(humanBytes(total));
    });
}

void DeviceWorker::readMemory(quint32 address, quint32 length)
{
    runJob(QStringLiteral("Reading %1 from 0x%2")
               .arg(humanBytes(length))
               .arg(address, 8, 16, QLatin1Char('0')),
           [&] {
               const auto data = session_->readMemory(
                   address, length,
                   [this](const ProgressReport& report) {
                       emit progress(static_cast<qint64>(report.done),
                                     static_cast<qint64>(report.total));
                   },
                   [this] { return cancel_.load(std::memory_order_relaxed); });

               const QByteArray bytes(reinterpret_cast<const char*>(data.data()),
                                      static_cast<qsizetype>(data.size()));
               emit memoryRead(bytes, address);
               return QStringLiteral("Read %1 from 0x%2")
                   .arg(humanBytes(static_cast<qint64>(data.size())))
                   .arg(address, 8, 16, QLatin1Char('0'));
           });
}

void DeviceWorker::writeMemory(quint32 address, const QString& file)
{
    runJob(QStringLiteral("Writing %1 to 0x%2")
               .arg(QFileInfo(file).fileName())
               .arg(address, 8, 16, QLatin1Char('0')),
           [&] {
               std::ifstream in(toPath(file), std::ios::binary);
               if (!in)
                   throw Error(ErrorCode::Io,
                               "Cannot open " + file.toStdString());
               std::vector<uint8_t> data(
                   (std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
               if (data.empty())
                   throw Error(ErrorCode::InvalidArgument, "The file is empty.");

               session_->writeMemory(
                   address, data,
                   [this](const ProgressReport& report) {
                       emit progress(static_cast<qint64>(report.done),
                                     static_cast<qint64>(report.total));
                   },
                   [this] { return cancel_.load(std::memory_order_relaxed); });

               return QStringLiteral("Wrote %1 to 0x%2")
                   .arg(humanBytes(static_cast<qint64>(data.size())))
                   .arg(address, 8, 16, QLatin1Char('0'));
           });
}

void DeviceWorker::call(quint32 address)
{
    runJob(QStringLiteral("Calling 0x%1").arg(address, 4, 16, QLatin1Char('0')),
           [&] {
               session_->call(address);
               return QStringLiteral("Called routine at 0x%1")
                   .arg(address, 4, 16, QLatin1Char('0'));
           });
}

void DeviceWorker::printOsd(int row, int column, const QString& text,
                            bool flash, bool necFont)
{
    runJob(QStringLiteral("Sending OSD text"), [&] {
        session_->printOsd(static_cast<uint8_t>(row),
                           static_cast<uint8_t>(column),
                           text.toStdString(), flash, necFont);
        return QStringLiteral("OSD text sent");
    });
}

} // namespace tpc2::gui
