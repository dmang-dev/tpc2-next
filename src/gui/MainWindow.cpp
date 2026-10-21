// SPDX-License-Identifier: MIT
#include "gui/MainWindow.h"

#include "gui/DeviceWorker.h"
#include "gui/RemotePage.h"
#include "gui/Theme.h"
#include "gui/TransferPage.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QThread>
#include <QTime>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace tpc2::gui {
namespace {

QLabel* makeLabel(const QString& text, const char* role)
{
    auto* label = new QLabel(text);
    label->setProperty("role", role);
    return label;
}

QString shortVersion()
{
    return QStringLiteral(TPC2_VERSION);
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("Root"));
    setWindowTitle(QStringLiteral("TPC2 Next %1 — Doctor V64 Workstation").arg(shortVersion()));
    setWindowIcon(Theme::appIcon());
    setMinimumSize(1024, 720);
    resize(1180, 860);

    buildUi();
    buildMenus();
    buildLogDock();
    buildStatusBar();
    wireSignals();

    QSettings settings;
    if (settings.contains(QStringLiteral("window/geometry")))
        restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    if (settings.contains(QStringLiteral("window/state")))
        restoreState(settings.value(QStringLiteral("window/state")).toByteArray());

    appendLog(QStringLiteral("TPC2 Next %1 ready").arg(shortVersion()),
              QStringLiteral("info"));
    appendLog(QStringLiteral(
                  "Select a backend and press Connect, or use Device ▸ Connect demo device."),
              QStringLiteral("info"));
    updateStatus();
}

MainWindow::~MainWindow()
{
    shutdownWorker();
}

void MainWindow::buildUi()
{
    auto* central = new QWidget;
    central->setObjectName(QStringLiteral("Root"));

    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(16, 10, 16, 4);
    layout->setSpacing(4);

    auto* brand = new QHBoxLayout;
    brand->setSpacing(10);

    auto* logo = new QLabel;
    logo->setPixmap(Theme::appIcon().pixmap(30, 30));
    logo->setFixedSize(30, 30);
    logo->setScaledContents(true);
    brand->addWidget(logo);

    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    titles->addWidget(makeLabel(QStringLiteral("TPC2 Next"), "h2"));
    titles->addWidget(makeLabel(
        QStringLiteral("Doctor V64 upload · download · remote control"), "tiny"));
    brand->addLayout(titles);
    brand->addStretch(1);
    brand->addWidget(makeLabel(QStringLiteral("TPC 2.0"), "tag"));
    brand->addWidget(makeLabel(QStringLiteral("GD6 / GD6E"), "tag"));
    brand->addWidget(makeLabel(QStringLiteral("Qt 6 · Windows · macOS · Linux"), "tiny"));

    layout->addLayout(brand);

    transfer_ = new TransferPage;
    remote_ = new RemotePage;

    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(true);
    tabs_->setTabPosition(QTabWidget::North);
    tabs_->addTab(transfer_, QStringLiteral("Transfer"));
    tabs_->addTab(remote_, QStringLiteral("Remote control"));
    tabs_->setTabToolTip(0, QStringLiteral("Send files to the V64 and receive DRAM images"));
    tabs_->setTabToolTip(1, QStringLiteral("Read/write memory, call routines and print on the OSD"));
    layout->addWidget(tabs_, 1);

    setCentralWidget(central);
}

void MainWindow::buildMenus()
{
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    auto* addAction = fileMenu->addAction(QStringLiteral("&Add files…"));
    addAction->setShortcut(QKeySequence::Open);
    connect(addAction, &QAction::triggered, this, &MainWindow::addFilesDialog);

    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(QStringLiteral("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &MainWindow::close);

    auto* deviceMenu = menuBar()->addMenu(QStringLiteral("&Device"));
    auto* demoAction = deviceMenu->addAction(QStringLiteral("Connect &demo device"));
    demoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(demoAction, &QAction::triggered, this, &MainWindow::connectDemo);

    auto* disconnectAction = deviceMenu->addAction(QStringLiteral("&Disconnect"));
    connect(disconnectAction, &QAction::triggered, this, [this] {
        if (worker_)
            QMetaObject::invokeMethod(worker_, &DeviceWorker::disconnectDevice,
                                      Qt::QueuedConnection);
    });

    deviceMenu->addSeparator();
    auto* refreshAction = deviceMenu->addAction(QStringLiteral("&Refresh serial ports"));
    refreshAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(refreshAction, &QAction::triggered, transfer_, &TransferPage::refreshPorts);

    viewMenu_ = menuBar()->addMenu(QStringLiteral("&View"));
    auto* transferAction = viewMenu_->addAction(QStringLiteral("&Transfer page"));
    transferAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    connect(transferAction, &QAction::triggered, this, [this] { showPage(0); });

    auto* remoteAction = viewMenu_->addAction(QStringLiteral("&Remote control page"));
    remoteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    connect(remoteAction, &QAction::triggered, this, [this] { showPage(1); });

    auto* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    auto* aboutAction = helpMenu->addAction(QStringLiteral("&About TPC2 Next"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::about);
    auto* aboutQtAction = helpMenu->addAction(QStringLiteral("About &Qt"));
    connect(aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);
}

void MainWindow::buildLogDock()
{
    logDock_ = new QDockWidget(QStringLiteral("ACTIVITY LOG"), this);
    logDock_->setObjectName(QStringLiteral("LogDock"));
    logDock_->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    logDock_->setFeatures(QDockWidget::DockWidgetMovable
                          | QDockWidget::DockWidgetFloatable
                          | QDockWidget::DockWidgetClosable);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setUndoRedoEnabled(false);
    log_->setMaximumBlockCount(5000);
    log_->setFrameShape(QFrame::NoFrame);
    log_->setPlaceholderText(QStringLiteral("Protocol activity appears here."));
    logDock_->setWidget(log_);

    addDockWidget(Qt::BottomDockWidgetArea, logDock_);
    resizeDocks({ logDock_ }, { 158 }, Qt::Vertical);

    if (viewMenu_) {
        viewMenu_->addSeparator();
        viewMenu_->addAction(logDock_->toggleViewAction());
        viewMenu_->addSeparator();
        auto* clearAction = viewMenu_->addAction(QStringLiteral("&Clear log"));
        clearAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
        connect(clearAction, &QAction::triggered, this, &MainWindow::clearLog);
    }
}

void MainWindow::buildStatusBar()
{
    statusConnection_ = new QLabel(QStringLiteral("OFFLINE"));
    statusConnection_->setProperty("role", "pill");
    statusConnection_->setProperty("state", "off");
    statusConnection_->setContentsMargins(2, 0, 2, 0);

    statusBackend_ = new QLabel(QStringLiteral("No device connected"));
    statusBackend_->setProperty("role", "tiny");

    statusBusy_ = new QLabel(QStringLiteral("Idle"));
    statusBusy_->setProperty("role", "tiny");

    statusBar()->addWidget(statusConnection_);
    statusBar()->addWidget(statusBackend_, 1);
    statusBar()->addPermanentWidget(statusBusy_);
}

void MainWindow::wireSignals()
{
    workerThread_ = new QThread(this);
    workerThread_->setObjectName(QStringLiteral("DeviceWorker"));
    worker_ = new DeviceWorker;
    worker_->moveToThread(workerThread_);
    workerThread_->start();

    connect(transfer_, &TransferPage::connectRequested,
            this, &MainWindow::onConnectRequested);
    connect(transfer_, &TransferPage::disconnectRequested, this, [this] {
        if (worker_)
            QMetaObject::invokeMethod(worker_, &DeviceWorker::disconnectDevice,
                                      Qt::QueuedConnection);
    });
    connect(transfer_, &TransferPage::uploadRequested, this,
            [this](const QStringList& paths, const QStringList& names) {
                if (!worker_)
                    return;
                transfer_->beginOperation(QStringLiteral("Sending to V64"));
                QMetaObject::invokeMethod(worker_, [this, paths, names] {
                    worker_->upload(paths, names);
                }, Qt::QueuedConnection);
            });
    connect(transfer_, &TransferPage::downloadRequested, this,
            [this](const QString& destination) {
                if (!worker_)
                    return;
                transfer_->beginOperation(QStringLiteral("Receiving from V64"));
                QMetaObject::invokeMethod(worker_, [this, destination] {
                    worker_->download(destination);
                }, Qt::QueuedConnection);
            });
    connect(transfer_, &TransferPage::cancelRequested, this, [this] {
        if (worker_)
            worker_->requestCancel();
    });
    connect(transfer_, &TransferPage::logMessage,
            this, &MainWindow::appendLog);

    connect(remote_, &RemotePage::readRequested, this,
            [this](quint32 address, quint32 length) {
                if (worker_)
                    QMetaObject::invokeMethod(worker_, [this, address, length] {
                        worker_->readMemory(address, length);
                    }, Qt::QueuedConnection);
            });
    connect(remote_, &RemotePage::writeRequested, this,
            [this](quint32 address, const QString& file) {
                if (worker_)
                    QMetaObject::invokeMethod(worker_, [this, address, file] {
                        worker_->writeMemory(address, file);
                    }, Qt::QueuedConnection);
            });
    connect(remote_, &RemotePage::callRequested, this, [this](quint32 address) {
        if (worker_)
            QMetaObject::invokeMethod(worker_, [this, address] {
                worker_->call(address);
            }, Qt::QueuedConnection);
    });
    connect(remote_, &RemotePage::printRequested, this,
            [this](int row, int column, const QString& text,
                   bool flash, bool necFont) {
                if (worker_)
                    QMetaObject::invokeMethod(
                        worker_, [this, row, column, text, flash, necFont] {
                            worker_->printOsd(row, column, text, flash, necFont);
                        }, Qt::QueuedConnection);
            });

    connect(worker_, &DeviceWorker::connected, this, &MainWindow::onConnected);
    connect(worker_, &DeviceWorker::disconnected, this, &MainWindow::onDisconnected);
    connect(worker_, &DeviceWorker::busyChanged, this, &MainWindow::onBusyChanged);
    connect(worker_, &DeviceWorker::progress, transfer_, &TransferPage::setProgress);
    connect(worker_, &DeviceWorker::logMessage, this, &MainWindow::appendLog);
    connect(worker_, &DeviceWorker::errorOccurred, this, &MainWindow::onError);
    connect(worker_, &DeviceWorker::memoryRead, remote_, &RemotePage::setMemory);
    connect(worker_, &DeviceWorker::operationFinished,
            this, &MainWindow::onOperationFinished);
}

void MainWindow::onConnectRequested(const BackendConfig& config)
{
    statusBackend_->setText(QStringLiteral("Connecting…"));
    appendLog(QStringLiteral("Connecting…"), QStringLiteral("info"));
    if (worker_)
        QMetaObject::invokeMethod(worker_, [this, config] {
            worker_->connectDevice(config);
        }, Qt::QueuedConnection);
}

void MainWindow::onConnected(const QString& description, int mode)
{
    connected_ = true;
    deviceDescription_ = description
        + QStringLiteral("  ·  ")
        + (mode == 0 ? QStringLiteral("old handshake") : QStringLiteral("new handshake"));
    statusBackend_->setText(deviceDescription_);
    transfer_->setConnected(true, description, mode);
    remote_->setConnected(true);
    updateStatus();
    statusBar()->showMessage(QStringLiteral("Connected to %1").arg(description), 5000);
}

void MainWindow::onDisconnected()
{
    connected_ = false;
    deviceDescription_.clear();
    statusBackend_->setText(QStringLiteral("No device connected"));
    transfer_->setConnected(false, QString(), 0);
    remote_->setConnected(false);
    updateStatus();
}

void MainWindow::onBusyChanged(bool busy)
{
    busy_ = busy;
    transfer_->setBusy(busy);
    remote_->setBusy(busy);
    updateStatus();
}

void MainWindow::onOperationFinished(const QString& summary)
{
    transfer_->finishUpload(true);
    transfer_->finishDownload(true);
    transfer_->resetProgress();
    statusBar()->showMessage(summary, 9000);
}

void MainWindow::onError(const QString& message)
{
    appendLog(message, QStringLiteral("error"));
    statusBar()->showMessage(QStringLiteral("Error: %1").arg(message), 12000);
}

void MainWindow::updateStatus()
{
    statusConnection_->setText(connected_ ? QStringLiteral("ONLINE")
                                          : QStringLiteral("OFFLINE"));
    statusConnection_->setProperty("state", connected_ ? "on" : "off");
    Theme::repolish(statusConnection_);
    statusBusy_->setText(busy_ ? QStringLiteral("Working…") : QStringLiteral("Idle"));
}

void MainWindow::connectDemo()
{
    if (busy_)
        return;
    transfer_->useDemoBackend();
    BackendConfig config;
    config.kind = BackendConfig::Kind::Demo;
    config.mode = 0;
    onConnectRequested(config);
}

void MainWindow::showPage(int index)
{
    if (tabs_ && index >= 0 && index < tabs_->count())
        tabs_->setCurrentIndex(index);
}

void MainWindow::addFiles(const QStringList& files)
{
    if (transfer_)
        transfer_->addFiles(files);
}

void MainWindow::addFilesDialog()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Select files to send to the V64"));
    if (!files.isEmpty()) {
        addFiles(files);
        showPage(0);
    }
}

void MainWindow::clearLog()
{
    if (log_)
        log_->clear();
}

void MainWindow::about()
{
    QMessageBox::about(
        this, QStringLiteral("About TPC2 Next"),
        QStringLiteral(
            "<h3>TPC2 Next %1</h3>"
            "<p>A modern, cross-platform replacement for Bung's <i>TPC2.EXE</i>, "
            "the original Doctor V64 upload and download utility.</p>"
            "<p><b>Protocols:</b> TPC 2.0 / GD6 with old and new handshakes, plus the "
            "GD6E extended commands (memory read/write, routine call and OSD print).</p>"
            "<p><b>Backends:</b> Raspberry Pi Pico USB bridge, legacy PC parallel port, "
            "and a built-in V64 simulator for demonstrations and tests.</p>"
            "<p><b>Built with:</b> Qt %2 and C++20.</p>"
            "<p>Original Bung software reference:<br>"
            "<a href=\"https://web.archive.org/web/19981212023426/http://www.bung.com.hk/\">"
            "web.archive.org · bung.com.hk (1998)</a></p>"
            "<p>TPC2 Next is released under the MIT licence. Doctor V64 and Bung are "
            "trademarks of their respective owners.</p>")
            .arg(shortVersion(), QString::fromLatin1(qVersion())));
}

void MainWindow::appendLog(const QString& message, const QString& level)
{
    if (!log_)
        return;

    QString color = QStringLiteral("#9aa2b8");
    QString tag = QStringLiteral("···");
    if (level == QStringLiteral("ok")) {
        color = QStringLiteral("#5eead4");
        tag = QStringLiteral(" OK");
    } else if (level == QStringLiteral("warn")) {
        color = QStringLiteral("#fbbf24");
        tag = QStringLiteral("WRN");
    } else if (level == QStringLiteral("error")) {
        color = QStringLiteral("#fca5a5");
        tag = QStringLiteral("ERR");
    } else if (level == QStringLiteral("info")) {
        color = QStringLiteral("#a5b4fc");
        tag = QStringLiteral("INF");
    }

    const QString stamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    log_->appendHtml(QStringLiteral("<span style=\"color:#5b6377\">%1</span> "
                                    "<span style=\"color:%2\">%3</span> "
                                    "<span style=\"color:%2\">%4</span>")
                         .arg(stamp, color, tag, message.toHtmlEscaped()));
}

void MainWindow::shutdownWorker()
{
    if (shuttingDown_ || !workerThread_)
        return;
    shuttingDown_ = true;

    DeviceWorker* worker = worker_;
    if (worker && workerThread_->isRunning()) {
        worker->requestCancel();
        QMetaObject::invokeMethod(worker, &DeviceWorker::shutdown,
                                  Qt::BlockingQueuedConnection);
    }
    workerThread_->quit();
    workerThread_->wait();

    delete worker;
    worker_ = nullptr;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (busy_) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Operation in progress"),
            QStringLiteral("A transfer is still running. Cancel it and quit?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }

    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());

    if (worker_)
        worker_->requestCancel();
    event->accept();
}

} // namespace tpc2::gui
