// SPDX-License-Identifier: MIT
#pragma once

#include "gui/BackendConfig.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

namespace tpc2 {
class Link;
class Session;
}

namespace tpc2::gui {

class DeviceWorker final : public QObject {
    Q_OBJECT

public:
    explicit DeviceWorker(QObject* parent = nullptr);
    ~DeviceWorker() override;

    // Called directly from the GUI thread. Safe because it only touches an
    // atomic flag; the worker polls it between protocol chunks.
    void requestCancel() { cancel_.store(true, std::memory_order_relaxed); }

public slots:
    void connectDevice(const BackendConfig& config);
    void disconnectDevice();
    void shutdown();

    void upload(const QStringList& paths, const QStringList& remoteNames);
    void download(const QString& destination);

    void readMemory(quint32 address, quint32 length);
    void writeMemory(quint32 address, const QString& file);
    void call(quint32 address);
    void printOsd(int row, int column, const QString& text,
                  bool flash, bool necFont);

signals:
    void connected(const QString& description, int mode);
    void disconnected();
    void busyChanged(bool busy);
    void progress(qint64 done, qint64 total);
    void logMessage(const QString& message, const QString& level);
    void errorOccurred(const QString& message);
    void memoryRead(const QByteArray& data, quint32 address);
    void operationFinished(const QString& summary);

private:
    void closeSession();
    void runJob(const QString& label, const std::function<QString()>& job);
    [[nodiscard]] bool ensureSession();

    std::unique_ptr<tpc2::Link> link_;
    std::unique_ptr<tpc2::Session> session_;
    std::atomic_bool cancel_ { false };
};

} // namespace tpc2::gui
