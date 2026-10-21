// SPDX-License-Identifier: MIT
#pragma once

#include "gui/BackendConfig.h"

#include <QMainWindow>
#include <QString>

class QCloseEvent;
class QDockWidget;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QTabWidget;
class QThread;

namespace tpc2::gui {

class DeviceWorker;
class RemotePage;
class TransferPage;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Connect the built-in simulator. Used by the demo action and by
    // --demo/--screenshot runs.
    void connectDemo();

    void showPage(int index);
    void addFiles(const QStringList& files);

    [[nodiscard]] bool isBusy() const { return busy_; }
    [[nodiscard]] bool isConnected() const { return connected_; }

public slots:
    void appendLog(const QString& message, const QString& level);

private:
    void buildUi();
    void buildMenus();
    void buildLogDock();
    void buildStatusBar();
    void wireSignals();
    void updateStatus();
    void shutdownWorker();

    void onConnectRequested(const BackendConfig& config);
    void onConnected(const QString& description, int mode);
    void onDisconnected();
    void onBusyChanged(bool busy);
    void onOperationFinished(const QString& summary);
    void onError(const QString& message);
    void addFilesDialog();
    void clearLog();
    void about();

    void closeEvent(QCloseEvent* event) override;

    QThread* workerThread_ = nullptr;
    DeviceWorker* worker_ = nullptr;

    QTabWidget* tabs_ = nullptr;
    TransferPage* transfer_ = nullptr;
    RemotePage* remote_ = nullptr;
    QDockWidget* logDock_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QMenu* viewMenu_ = nullptr;

    QLabel* statusConnection_ = nullptr;
    QLabel* statusBackend_ = nullptr;
    QLabel* statusBusy_ = nullptr;

    QString deviceDescription_;
    bool connected_ = false;
    bool busy_ = false;
    bool shuttingDown_ = false;
};

} // namespace tpc2::gui
