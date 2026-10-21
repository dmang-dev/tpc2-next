// SPDX-License-Identifier: MIT
#pragma once

#include "gui/BackendConfig.h"

#include <QElapsedTimer>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace tpc2::gui {

class DropTable;

class TransferPage final : public QWidget {
    Q_OBJECT

public:
    explicit TransferPage(QWidget* parent = nullptr);

    void setConnected(bool connected, const QString& description, int mode);
    void setBusy(bool busy);
    void setProgress(qint64 done, qint64 total);
    void resetProgress();
    void beginOperation(const QString& label);
    void finishUpload(bool success);
    void finishDownload(bool success);
    void addFiles(const QStringList& files);
    void refreshPorts();
    void useDemoBackend();

    [[nodiscard]] bool isConnected() const { return connected_; }
    [[nodiscard]] bool isBusy() const { return busy_; }

signals:
    void connectRequested(const tpc2::gui::BackendConfig& config);
    void disconnectRequested();
    void uploadRequested(const QStringList& paths, const QStringList& names);
    void downloadRequested(const QString& destination);
    void cancelRequested();
    void logMessage(const QString& message, const QString& level);

private:
    void buildUi();
    void loadSettings();
    void saveSettings();
    void updateConnectionUi();
    void updateActionState();
    void onBackendChanged();
    void onSend();
    void onReceive();
    void onConnectClicked();
    void removeSelected();
    void clearFiles();
    [[nodiscard]] BackendConfig currentConfig() const;
    [[nodiscard]] quint16 customLptBase() const;
    [[nodiscard]] QString destination() const;
    [[nodiscard]] int uploadRowCount() const;

    bool connected_ = false;
    bool busy_ = false;
    QString description_;
    int mode_ = 0;

    QComboBox* backendCombo_ = nullptr;
    QStackedWidget* backendStack_ = nullptr;
    QComboBox* portCombo_ = nullptr;
    QComboBox* lptCombo_ = nullptr;
    QLineEdit* customLptEdit_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QLabel* statusPill_ = nullptr;
    QLabel* deviceLabel_ = nullptr;
    QLabel* backendHint_ = nullptr;

    DropTable* table_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* sendButton_ = nullptr;
    QLineEdit* destinationEdit_ = nullptr;
    QPushButton* receiveButton_ = nullptr;

    QLabel* operationLabel_ = nullptr;
    QLabel* progressDetail_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QPushButton* cancelButton_ = nullptr;

    QElapsedTimer progressTimer_;
    qint64 lastDone_ = 0;
    qint64 lastSampleMs_ = 0;
    double speed_ = 0.0;
    bool operationActive_ = false;
};

} // namespace tpc2::gui
