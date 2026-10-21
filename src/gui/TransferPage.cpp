// SPDX-License-Identifier: MIT
#include "gui/TransferPage.h"

#include "core/Protocol.h"
#include "gui/Theme.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace tpc2::gui {
namespace {

QLabel* makeLabel(const QString& text, const char* role)
{
    auto* label = new QLabel(text);
    label->setProperty("role", role);
    return label;
}

QFrame* makeCard(bool accent = false)
{
    auto* card = new QFrame;
    card->setProperty("card", true);
    if (accent)
        card->setProperty("accent", true);
    return card;
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
    return QString::number(amount, 'f', unit == 0 ? 0 : 1) + " " + units[unit];
}

QString formatDuration(qint64 seconds)
{
    if (seconds < 0)
        return QStringLiteral("--:--");
    if (seconds >= 3600)
        return QStringLiteral("%1:%2:%3")
            .arg(seconds / 3600)
            .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2")
        .arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

bool parseNumber(const QString& text, quint32& value)
{
    bool ok = false;
    const QString trimmed = text.trimmed();
    if (trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        value = trimmed.mid(2).toUInt(&ok, 16);
    else
        value = trimmed.toUInt(&ok, 10);
    return ok;
}

} // namespace

class DropTable final : public QTableWidget {
public:
    explicit DropTable(QWidget* parent = nullptr)
        : QTableWidget(parent)
    {
        setAcceptDrops(true);
        setDragDropMode(QAbstractItemView::DropOnly);
        setDropIndicatorShown(false);
    }

    std::function<void(const QStringList&)> onFilesDropped;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData()->hasUrls()) {
            event->acceptProposedAction();
            return;
        }
        QTableWidget::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (event->mimeData()->hasUrls()) {
            event->acceptProposedAction();
            return;
        }
        QTableWidget::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        QStringList files;
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile())
                files.push_back(url.toLocalFile());
        }
        if (!files.isEmpty() && onFilesDropped) {
            onFilesDropped(files);
            event->acceptProposedAction();
            return;
        }
        QTableWidget::dropEvent(event);
    }
};

TransferPage::TransferPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("Page"));
    buildUi();
    loadSettings();
    refreshPorts();
    updateConnectionUi();
    resetProgress();
}

void TransferPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(14);

    // ---------------------------------------------------------------------
    // Connection card
    // ---------------------------------------------------------------------
    auto* connection = makeCard(true);
    auto* connectionLayout = new QVBoxLayout(connection);
    connectionLayout->setContentsMargins(18, 16, 18, 16);
    connectionLayout->setSpacing(10);

    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(10);
    topRow->addWidget(makeLabel(QStringLiteral("CONNECTION"), "tiny"));
    statusPill_ = makeLabel(QStringLiteral("OFFLINE"), "pill");
    statusPill_->setProperty("state", "off");
    topRow->addWidget(statusPill_);
    deviceLabel_ = makeLabel(QStringLiteral("Not connected"), "sub");
    topRow->addWidget(deviceLabel_);
    topRow->addStretch(1);
    auto* protocolTag = makeLabel(QStringLiteral("TPC 2.0 / GD6"), "tag");
    topRow->addWidget(protocolTag);
    connectionLayout->addLayout(topRow);

    auto* controls = new QHBoxLayout;
    controls->setSpacing(10);

    controls->addWidget(makeLabel(QStringLiteral("Backend"), "sub"));
    backendCombo_ = new QComboBox;
    backendCombo_->addItem(QStringLiteral("USB bridge"), static_cast<int>(BackendConfig::Kind::Bridge));
    backendCombo_->addItem(QStringLiteral("Parallel port"), static_cast<int>(BackendConfig::Kind::Parallel));
    backendCombo_->addItem(QStringLiteral("Demo device"), static_cast<int>(BackendConfig::Kind::Demo));
    backendCombo_->setMinimumWidth(150);
    controls->addWidget(backendCombo_);

    backendStack_ = new QStackedWidget;
    backendStack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* bridgePage = new QWidget;
    auto* bridgeLayout = new QHBoxLayout(bridgePage);
    bridgeLayout->setContentsMargins(0, 0, 0, 0);
    bridgeLayout->setSpacing(6);
    portCombo_ = new QComboBox;
    portCombo_->setMinimumWidth(190);
    bridgeLayout->addWidget(portCombo_, 1);
    auto* refreshButton = new QPushButton(QStringLiteral("Refresh"));
    refreshButton->setToolTip(QStringLiteral("Look for a newly plugged in USB bridge"));
    bridgeLayout->addWidget(refreshButton);
    connect(refreshButton, &QPushButton::clicked, this, &TransferPage::refreshPorts);
    backendStack_->addWidget(bridgePage);

    auto* parallelPage = new QWidget;
    auto* parallelLayout = new QHBoxLayout(parallelPage);
    parallelLayout->setContentsMargins(0, 0, 0, 0);
    parallelLayout->setSpacing(6);
    lptCombo_ = new QComboBox;
    lptCombo_->addItem(QStringLiteral("LPT1  ·  0x378"), 0x378);
    lptCombo_->addItem(QStringLiteral("LPT2  ·  0x278"), 0x278);
    lptCombo_->addItem(QStringLiteral("LPT3  ·  0x3BC"), 0x3BC);
    lptCombo_->addItem(QStringLiteral("Custom…"), 0);
    parallelLayout->addWidget(lptCombo_);
    customLptEdit_ = new QLineEdit(QStringLiteral("0x378"));
    customLptEdit_->setMaximumWidth(90);
    customLptEdit_->setVisible(false);
    parallelLayout->addWidget(customLptEdit_);
    auto* lptHint = makeLabel(QStringLiteral("legacy hardware · InpOut/root required"), "tiny");
    parallelLayout->addWidget(lptHint, 1);
    backendStack_->addWidget(parallelPage);

    auto* demoPage = new QWidget;
    auto* demoLayout = new QHBoxLayout(demoPage);
    demoLayout->setContentsMargins(0, 0, 0, 0);
    demoLayout->addWidget(makeLabel(
        QStringLiteral("Simulated V64 · upload, download and remote commands work without hardware"), "tiny"));
    backendStack_->addWidget(demoPage);

    controls->addWidget(backendStack_, 1);

    controls->addWidget(makeLabel(QStringLiteral("Handshake"), "sub"));
    modeCombo_ = new QComboBox;
    modeCombo_->addItem(QStringLiteral("Auto"));
    modeCombo_->addItem(QStringLiteral("Old (pre-1.2x)"));
    modeCombo_->addItem(QStringLiteral("New (1.2x+)"));
    controls->addWidget(modeCombo_);

    connectButton_ = new QPushButton(QStringLiteral("Connect"));
    connectButton_->setProperty("accent", true);
    connectButton_->setMinimumWidth(110);
    controls->addWidget(connectButton_);
    connectionLayout->addLayout(controls);

    backendHint_ = makeLabel(QString(), "tiny");
    backendHint_->setWordWrap(true);
    connectionLayout->addWidget(backendHint_);

    root->addWidget(connection);

    // ---------------------------------------------------------------------
    // Send / receive cards
    // ---------------------------------------------------------------------
    auto* transferRow = new QHBoxLayout;
    transferRow->setSpacing(14);

    auto* sendCard = makeCard();
    sendCard->setMinimumWidth(560);
    auto* sendLayout = new QVBoxLayout(sendCard);
    sendLayout->setContentsMargins(18, 16, 18, 16);
    sendLayout->setSpacing(9);

    auto* sendHeader = new QHBoxLayout;
    sendHeader->addWidget(makeLabel(QStringLiteral("Send to V64"), "h2"));
    sendHeader->addStretch(1);
    sendHeader->addWidget(makeLabel(QStringLiteral("TPC / GD6R"), "tag"));
    sendLayout->addLayout(sendHeader);
    sendLayout->addWidget(makeLabel(
        QStringLiteral("Drag files here or add them below. Names are converted to 8.3 automatically."),
        "sub"));

    table_ = new DropTable;
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(
        { QStringLiteral("LOCAL FILE"), QStringLiteral("REMOTE NAME"),
          QStringLiteral("SIZE"), QStringLiteral("STATUS") });
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->setColumnWidth(1, 150);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(false);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked
                            | QAbstractItemView::SelectedClicked
                            | QAbstractItemView::EditKeyPressed);
    table_->setMinimumHeight(190);
    table_->onFilesDropped = [this](const QStringList& files) { addFiles(files); };
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
        updateActionState();
    });
    sendLayout->addWidget(table_, 1);

    auto* sendButtons = new QHBoxLayout;
    sendButtons->setSpacing(7);
    addButton_ = new QPushButton(QStringLiteral("Add files…"));
    removeButton_ = new QPushButton(QStringLiteral("Remove"));
    clearButton_ = new QPushButton(QStringLiteral("Clear"));
    sendButton_ = new QPushButton(QStringLiteral("Send all"));
    sendButton_->setProperty("accent", true);
    sendButtons->addWidget(addButton_);
    sendButtons->addWidget(removeButton_);
    sendButtons->addWidget(clearButton_);
    sendButtons->addStretch(1);
    sendButtons->addWidget(sendButton_);
    sendLayout->addLayout(sendButtons);
    transferRow->addWidget(sendCard, 3);

    auto* receiveCard = makeCard();
    receiveCard->setMinimumWidth(330);
    auto* receiveLayout = new QVBoxLayout(receiveCard);
    receiveLayout->setContentsMargins(18, 16, 18, 16);
    receiveLayout->setSpacing(10);

    auto* receiveHeader = new QHBoxLayout;
    receiveHeader->addWidget(makeLabel(QStringLiteral("Receive from V64"), "h2"));
    receiveHeader->addStretch(1);
    receiveHeader->addWidget(makeLabel(QStringLiteral("TPC / GD6W"), "tag"));
    receiveLayout->addLayout(receiveHeader);
    auto* explanation = makeLabel(
        QStringLiteral(
            "1.  Load the game or BIOS on the Doctor V64.\n"
            "2.  Press the  >>  button on the front panel.\n"
            "3.  Pick a folder and click Receive."),
        "sub");
    explanation->setWordWrap(true);
    receiveLayout->addWidget(explanation);
    receiveLayout->addSpacing(2);

    auto* destinationLabel = makeLabel(QStringLiteral("DESTINATION FOLDER"), "tiny");
    receiveLayout->addWidget(destinationLabel);
    auto* destinationRow = new QHBoxLayout;
    destinationRow->setSpacing(6);
    destinationEdit_ = new QLineEdit;
    destinationEdit_->setPlaceholderText(QStringLiteral("Choose a folder for the dump"));
    destinationRow->addWidget(destinationEdit_, 1);
    auto* browseButton = new QPushButton(QStringLiteral("Browse"));
    destinationRow->addWidget(browseButton);
    receiveLayout->addLayout(destinationRow);

    receiveButton_ = new QPushButton(QStringLiteral("Receive DRAM image"));
    receiveButton_->setProperty("accent", true);
    receiveButton_->setMinimumHeight(38);
    receiveLayout->addWidget(receiveButton_);
    auto* note = makeLabel(
        QStringLiteral("The V64 pushes its current DRAM image. The PC cannot browse "
                       "the device; start the transfer from the V64 front panel."),
        "tiny");
    note->setWordWrap(true);
    receiveLayout->addWidget(note);
    receiveLayout->addStretch(1);
    transferRow->addWidget(receiveCard, 2);

    root->addLayout(transferRow, 1);

    // ---------------------------------------------------------------------
    // Progress strip
    // ---------------------------------------------------------------------
    auto* progressCard = makeCard();
    auto* progressLayout = new QHBoxLayout(progressCard);
    progressLayout->setContentsMargins(18, 12, 18, 12);
    progressLayout->setSpacing(12);
    operationLabel_ = makeLabel(QStringLiteral("Ready"), "h2");
    operationLabel_->setMinimumWidth(190);
    progressLayout->addWidget(operationLabel_);
    progressBar_ = new QProgressBar;
    progressBar_->setTextVisible(false);
    progressBar_->setRange(0, 1000);
    progressBar_->setValue(0);
    progressLayout->addWidget(progressBar_, 1);
    progressDetail_ = makeLabel(QStringLiteral("No transfer in progress"), "sub");
    progressDetail_->setMinimumWidth(260);
    progressLayout->addWidget(progressDetail_);
    cancelButton_ = new QPushButton(QStringLiteral("Cancel"));
    cancelButton_->setProperty("danger", true);
    cancelButton_->setVisible(false);
    progressLayout->addWidget(cancelButton_);
    root->addWidget(progressCard);

    // ---------------------------------------------------------------------
    // Wiring
    // ---------------------------------------------------------------------
    connect(backendCombo_, &QComboBox::currentIndexChanged,
            this, &TransferPage::onBackendChanged);
    connect(lptCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        customLptEdit_->setVisible(lptCombo_->itemData(index).toInt() == 0);
        updateConnectionUi();
    });
    connect(modeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        saveSettings();
    });
    connect(connectButton_, &QPushButton::clicked, this, &TransferPage::onConnectClicked);
    connect(addButton_, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("Select files to send to the V64"));
        addFiles(files);
    });
    connect(removeButton_, &QPushButton::clicked, this, &TransferPage::removeSelected);
    connect(clearButton_, &QPushButton::clicked, this, &TransferPage::clearFiles);
    connect(sendButton_, &QPushButton::clicked, this, &TransferPage::onSend);
    connect(receiveButton_, &QPushButton::clicked, this, &TransferPage::onReceive);
    connect(cancelButton_, &QPushButton::clicked, this, [this] {
        operationLabel_->setText(QStringLiteral("Cancelling…"));
        cancelButton_->setEnabled(false);
        emit cancelRequested();
    });
    connect(browseButton, &QPushButton::clicked, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Choose a destination folder"), destinationEdit_->text());
        if (!folder.isEmpty()) {
            destinationEdit_->setText(folder);
            saveSettings();
        }
    });
    connect(destinationEdit_, &QLineEdit::editingFinished, this, [this] { saveSettings(); });
    connect(portCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        saveSettings();
        updateConnectionUi();
    });
    connect(customLptEdit_, &QLineEdit::editingFinished, this, [this] {
        saveSettings();
        updateConnectionUi();
    });
}

void TransferPage::loadSettings()
{
    QSettings settings;

    // Load the destination before touching any combo box. Changing a combo
    // emits currentIndexChanged, which saves the current form state; if the
    // destination is still empty at that point it would overwrite the saved
    // folder with an empty value and fall back to Documents on the next read.
    QString destination = settings.value(QStringLiteral("transfer/destination")).toString();
    if (destination.isEmpty()) {
        destination = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QStringLiteral("/V64 Dumps");
    }
    destinationEdit_->setText(QDir::toNativeSeparators(destination));

    const int backend = settings.value(QStringLiteral("connection/backend"), 1).toInt();
    const int backendIndex = backendCombo_->findData(backend);
    backendCombo_->setCurrentIndex(backendIndex >= 0 ? backendIndex : 0);

    const int mode = settings.value(QStringLiteral("connection/mode"), 0).toInt();
    modeCombo_->setCurrentIndex(std::clamp(mode, 0, 2));

    const QString port = settings.value(QStringLiteral("connection/port")).toString();
    if (!port.isEmpty()) {
        const int index = portCombo_->findData(port);
        if (index >= 0)
            portCombo_->setCurrentIndex(index);
    }

    const QString lpt = settings.value(QStringLiteral("connection/lpt"), QStringLiteral("0x378")).toString();
    bool custom = true;
    for (int i = 0; i < lptCombo_->count(); ++i) {
        quint32 value = 0;
        if (parseNumber(lpt, value) && lptCombo_->itemData(i).toUInt() == value && value != 0) {
            lptCombo_->setCurrentIndex(i);
            custom = false;
            break;
        }
    }
    if (custom) {
        lptCombo_->setCurrentIndex(lptCombo_->count() - 1);
        customLptEdit_->setText(lpt);
        customLptEdit_->setVisible(true);
    }

    onBackendChanged();
}

void TransferPage::saveSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("connection/backend"),
                      backendCombo_->currentData().toInt());
    settings.setValue(QStringLiteral("connection/mode"), modeCombo_->currentIndex());
    settings.setValue(QStringLiteral("connection/port"), portCombo_->currentData().toString());
    settings.setValue(QStringLiteral("connection/lpt"), customLptEdit_->text());
    settings.setValue(QStringLiteral("transfer/destination"), destinationEdit_->text());
}

void TransferPage::refreshPorts()
{
    const QString previous = portCombo_->currentData().toString();
    portCombo_->blockSignals(true);
    portCombo_->clear();
    const auto ports = QSerialPortInfo::availablePorts();
    for (const auto& port : ports) {
        QString label = port.portName();
        if (!port.description().isEmpty())
            label += QStringLiteral("  ·  ") + port.description();
        portCombo_->addItem(label, port.portName());
        portCombo_->setItemData(portCombo_->count() - 1, port.description(), Qt::ToolTipRole);
    }
    if (ports.isEmpty()) {
        portCombo_->addItem(QStringLiteral("No serial ports found"), QString());
    } else if (!previous.isEmpty()) {
        const int index = portCombo_->findData(previous);
        if (index >= 0)
            portCombo_->setCurrentIndex(index);
    }
    portCombo_->blockSignals(false);
    updateConnectionUi();
}

void TransferPage::useDemoBackend()
{
    const int index = backendCombo_->findData(
        static_cast<int>(BackendConfig::Kind::Demo));
    if (index < 0)
        return;
    if (backendCombo_->currentIndex() == index)
        onBackendChanged();
    else
        backendCombo_->setCurrentIndex(index);
}

void TransferPage::onBackendChanged()
{
    const int kind = backendCombo_->currentData().toInt();
    if (kind == static_cast<int>(BackendConfig::Kind::Bridge))
        backendStack_->setCurrentIndex(0);
    else if (kind == static_cast<int>(BackendConfig::Kind::Parallel))
        backendStack_->setCurrentIndex(1);
    else
        backendStack_->setCurrentIndex(2);

    if (kind == static_cast<int>(BackendConfig::Kind::Demo)) {
        backendHint_->setText(QStringLiteral(
            "Demo mode runs a built-in V64 simulator: uploads appear in the download list "
            "and remote commands are accepted. No cable is required."));
    } else if (kind == static_cast<int>(BackendConfig::Kind::Bridge)) {
        backendHint_->setText(QStringLiteral(
            "Connect the Pico-based bridge to the V64 with a full-pin DB25 cable. "
            "The bridge handles the time-critical handshake; no driver or admin rights are needed."));
    } else {
        backendHint_->setText(QStringLiteral(
            "Legacy parallel port only. Windows needs InpOut32/InpOutx64 and its driver; "
            "Linux needs root or /dev/port access. USB-to-parallel adapters generally do not work."));
    }
    saveSettings();
    updateConnectionUi();
}

void TransferPage::onConnectClicked()
{
    if (connected_) {
        emit disconnectRequested();
        return;
    }
    const BackendConfig config = currentConfig();
    if (config.kind == BackendConfig::Kind::Bridge && config.port.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("No serial port"),
                             QStringLiteral("Select the USB bridge serial port first. "
                                            "Plug in the bridge and press Refresh."));
        return;
    }
    if (config.kind == BackendConfig::Kind::Parallel && config.lptBase == 0) {
        QMessageBox::warning(this, QStringLiteral("Invalid port address"),
                             QStringLiteral("Enter a valid hexadecimal LPT base address, "
                                            "for example 0x378."));
        return;
    }
    emit connectRequested(config);
}

quint16 TransferPage::customLptBase() const
{
    quint32 value = 0;
    if (!parseNumber(customLptEdit_->text(), value) || value == 0 || value > 0xFFFF)
        return 0;
    return static_cast<quint16>(value);
}

BackendConfig TransferPage::currentConfig() const
{
    BackendConfig config;
    config.kind = static_cast<BackendConfig::Kind>(backendCombo_->currentData().toInt());
    config.port = portCombo_->currentData().toString();
    config.baud = 115200;
    config.mode = modeCombo_->currentIndex();
    if (lptCombo_->currentData().toInt() == 0)
        config.lptBase = customLptBase();
    else
        config.lptBase = static_cast<quint16>(lptCombo_->currentData().toInt());
    return config;
}

void TransferPage::setConnected(bool connected, const QString& description, int mode)
{
    connected_ = connected;
    description_ = description;
    mode_ = mode;
    updateConnectionUi();
    if (!connected && !busy_)
        resetProgress();
}

void TransferPage::updateConnectionUi()
{
    statusPill_->setText(connected_ ? QStringLiteral("ONLINE") : QStringLiteral("OFFLINE"));
    statusPill_->setProperty("state", connected_ ? "on" : "off");
    Theme::repolish(statusPill_);

    if (connected_) {
        deviceLabel_->setText(description_
            + QStringLiteral("  ·  ")
            + (mode_ == 0 ? QStringLiteral("old handshake") : QStringLiteral("new handshake")));
        connectButton_->setText(QStringLiteral("Disconnect"));
    } else {
        deviceLabel_->setText(QStringLiteral("Not connected"));
        connectButton_->setText(QStringLiteral("Connect"));
    }

    const bool bridge = backendCombo_->currentData().toInt()
        == static_cast<int>(BackendConfig::Kind::Bridge);
    const bool bridgeReady = !bridge || !portCombo_->currentData().toString().isEmpty();

    backendCombo_->setEnabled(!connected_ && !busy_);
    modeCombo_->setEnabled(!connected_ && !busy_);
    backendStack_->setEnabled(!connected_ && !busy_);
    connectButton_->setEnabled(!busy_ && (connected_ || bridgeReady));

    updateActionState();
    saveSettings();
}

void TransferPage::setBusy(bool busy)
{
    busy_ = busy;
    if (busy) {
        if (!operationActive_) {
            operationLabel_->setText(QStringLiteral("Working…"));
            progressDetail_->setText(QStringLiteral("Contacting the device…"));
        }
        cancelButton_->setVisible(true);
        cancelButton_->setEnabled(true);
    } else {
        cancelButton_->setVisible(false);
        if (!operationActive_)
            resetProgress();
    }
    table_->setEditTriggers(busy ? QAbstractItemView::NoEditTriggers
                                 : (QAbstractItemView::DoubleClicked
                                    | QAbstractItemView::SelectedClicked
                                    | QAbstractItemView::EditKeyPressed));
    updateConnectionUi();
}

void TransferPage::updateActionState()
{
    const bool hasRows = table_->rowCount() > 0;
    const bool canSend = connected_ && !busy_ && hasRows;
    if (addButton_)
        addButton_->setEnabled(!busy_);
    if (removeButton_)
        removeButton_->setEnabled(!busy_ && table_->selectionModel()
                                  && !table_->selectionModel()->selectedRows().isEmpty());
    if (clearButton_)
        clearButton_->setEnabled(!busy_ && hasRows);
    if (sendButton_)
        sendButton_->setEnabled(canSend);
    if (receiveButton_)
        receiveButton_->setEnabled(connected_ && !busy_);
    if (destinationEdit_)
        destinationEdit_->setEnabled(!busy_);
}

void TransferPage::beginOperation(const QString& label)
{
    operationActive_ = true;
    lastDone_ = 0;
    lastSampleMs_ = 0;
    speed_ = 0.0;
    progressTimer_.restart();
    operationLabel_->setText(label);
    progressBar_->setRange(0, 0);
    progressBar_->setValue(0);
    progressDetail_->setText(QStringLiteral("Starting…"));
    cancelButton_->setVisible(true);
    cancelButton_->setEnabled(true);
}

void TransferPage::setProgress(qint64 done, qint64 total)
{
    if (!operationActive_)
        beginOperation(operationLabel_->text());
    cancelButton_->setVisible(true);

    if (total > 0) {
        const int value = static_cast<int>(
            std::clamp<double>(1000.0 * static_cast<double>(done)
                                   / static_cast<double>(total),
                               0.0, 1000.0));
        progressBar_->setRange(0, 1000);
        progressBar_->setValue(value);
    } else {
        progressBar_->setRange(0, 0);
    }

    const qint64 now = progressTimer_.elapsed();
    if (lastSampleMs_ == 0) {
        lastSampleMs_ = now;
        lastDone_ = done;
    } else if (now - lastSampleMs_ >= 250) {
        const double instant = static_cast<double>(done - lastDone_)
            * 1000.0 / static_cast<double>(now - lastSampleMs_);
        speed_ = speed_ == 0.0 ? instant : (speed_ * 0.7 + instant * 0.3);
        lastDone_ = done;
        lastSampleMs_ = now;
    }

    QString detail;
    if (total > 0) {
        const int percent = static_cast<int>(100.0 * static_cast<double>(done)
                                             / static_cast<double>(total));
        detail = QStringLiteral("%1 / %2  ·  %3%")
            .arg(humanBytes(done), humanBytes(total))
            .arg(percent);
        if (speed_ > 1.0) {
            const qint64 remaining = static_cast<qint64>(
                static_cast<double>(total - done) / speed_);
            detail += QStringLiteral("  ·  %1/s  ·  ETA %2")
                .arg(humanBytes(static_cast<qint64>(speed_)))
                .arg(formatDuration(remaining));
        }
    } else {
        detail = QStringLiteral("%1 received").arg(humanBytes(done));
        if (speed_ > 1.0)
            detail += QStringLiteral("  ·  %1/s").arg(humanBytes(static_cast<qint64>(speed_)));
    }
    progressDetail_->setText(detail);
}

void TransferPage::resetProgress()
{
    operationActive_ = false;
    progressBar_->setRange(0, 1000);
    progressBar_->setValue(0);
    operationLabel_->setText(QStringLiteral("Ready"));
    progressDetail_->setText(QStringLiteral("No transfer in progress"));
    cancelButton_->setVisible(false);
    cancelButton_->setEnabled(true);
}

void TransferPage::finishUpload(bool success)
{
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto* item = table_->item(row, 3);
        if (item && item->text() == QStringLiteral("Sending"))
            item->setText(success ? QStringLiteral("Sent") : QStringLiteral("Failed"));
    }
    updateActionState();
}

void TransferPage::finishDownload(bool success)
{
    Q_UNUSED(success);
}

int TransferPage::uploadRowCount() const
{
    return table_->rowCount();
}

void TransferPage::addFiles(const QStringList& files)
{
    int added = 0;
    for (const QString& file : files) {
        const QFileInfo info(file);
        if (!info.isFile())
            continue;

        bool duplicate = false;
        for (int row = 0; row < table_->rowCount(); ++row) {
            if (table_->item(row, 0)->data(Qt::UserRole).toString() == file) {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;

        QString remoteName = info.fileName();
        try {
            const auto encoded = proto::encodeName(remoteName.toStdString());
            remoteName = QString::fromStdString(
                proto::decodeName(encoded.data(), encoded.size()));
        } catch (const std::exception& error) {
            QMessageBox::warning(this, QStringLiteral("Invalid file name"),
                                 QString::fromStdString(error.what()));
            continue;
        }

        const int row = table_->rowCount();
        table_->insertRow(row);

        auto* local = new QTableWidgetItem(info.fileName());
        local->setData(Qt::UserRole, file);
        local->setToolTip(file);
        local->setFlags(local->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 0, local);

        auto* remote = new QTableWidgetItem(remoteName);
        remote->setToolTip(QStringLiteral("8.3 name stored on the V64. Double-click to edit."));
        table_->setItem(row, 1, remote);

        auto* size = new QTableWidgetItem(humanBytes(info.size()));
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        size->setFlags(size->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 2, size);

        auto* status = new QTableWidgetItem(QStringLiteral("Queued"));
        status->setFlags(status->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 3, status);
        ++added;
    }
    updateActionState();
    if (added > 0)
        emit logMessage(QStringLiteral("Added %1 file(s) to the send queue").arg(added),
                        QStringLiteral("info"));
}

void TransferPage::removeSelected()
{
    auto selected = table_->selectionModel()
        ? table_->selectionModel()->selectedRows()
        : QModelIndexList {};
    std::sort(selected.begin(), selected.end(),
              [](const QModelIndex& a, const QModelIndex& b) {
                  return a.row() > b.row();
              });
    for (const QModelIndex& index : selected)
        table_->removeRow(index.row());
    updateActionState();
}

void TransferPage::clearFiles()
{
    table_->setRowCount(0);
    updateActionState();
}

void TransferPage::onSend()
{
    if (!connected_) {
        QMessageBox::warning(this, QStringLiteral("Not connected"),
                             QStringLiteral("Connect to the V64 first."));
        return;
    }
    if (table_->rowCount() == 0) {
        QMessageBox::information(this, QStringLiteral("Nothing to send"),
                                 QStringLiteral("Add at least one file."));
        return;
    }

    QStringList paths;
    QStringList names;
    QList<QByteArray> encodedNames;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const QString path = table_->item(row, 0)->data(Qt::UserRole).toString();
        QString name = table_->item(row, 1)->text().trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Missing name"),
                                 QStringLiteral("Row %1 has an empty remote name.").arg(row + 1));
            return;
        }
        try {
            const auto encoded = proto::encodeName(name.toStdString());
            const std::string decoded = proto::decodeName(encoded.data(), encoded.size());
            name = QString::fromStdString(decoded);
            encodedNames.push_back(QByteArray(
                reinterpret_cast<const char*>(encoded.data()),
                static_cast<qsizetype>(encoded.size())));
        } catch (const std::exception& error) {
            QMessageBox::warning(this, QStringLiteral("Invalid name"),
                                 QStringLiteral("Row %1: %2")
                                     .arg(row + 1)
                                     .arg(QString::fromStdString(error.what())));
            return;
        }
        table_->item(row, 1)->setText(name);
        table_->item(row, 3)->setText(QStringLiteral("Sending"));
        paths.push_back(path);
        names.push_back(name);
    }

    for (int i = 0; i < encodedNames.size(); ++i) {
        for (int j = i + 1; j < encodedNames.size(); ++j) {
            if (encodedNames[i] == encodedNames[j]) {
                QMessageBox::warning(
                    this, QStringLiteral("Duplicate 8.3 name"),
                    QStringLiteral("Two files map to the same V64 name (%1). "
                                   "Rename one of them before sending.")
                        .arg(names[i]));
                for (int row = 0; row < table_->rowCount(); ++row)
                    table_->item(row, 3)->setText(QStringLiteral("Queued"));
                return;
            }
        }
    }

    emit uploadRequested(paths, names);
}

void TransferPage::onReceive()
{
    if (!connected_) {
        QMessageBox::warning(this, QStringLiteral("Not connected"),
                             QStringLiteral("Connect to the V64 first."));
        return;
    }
    const QString folder = destinationEdit_->text().trimmed();
    if (folder.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("No destination"),
                             QStringLiteral("Choose a destination folder."));
        return;
    }
    emit downloadRequested(QDir::fromNativeSeparators(folder));
}

QString TransferPage::destination() const
{
    return destinationEdit_->text();
}

} // namespace tpc2::gui
