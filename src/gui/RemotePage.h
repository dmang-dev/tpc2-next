// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace tpc2::gui {

class RemotePage final : public QWidget {
    Q_OBJECT

public:
    explicit RemotePage(QWidget* parent = nullptr);

    void setConnected(bool connected);
    void setBusy(bool busy);
    void setMemory(const QByteArray& bytes, quint32 address);

signals:
    void readRequested(quint32 address, quint32 length);
    void writeRequested(quint32 address, const QString& file);
    void callRequested(quint32 address);
    void printRequested(int row, int column, const QString& text,
                        bool flash, bool necFont);

private:
    void buildUi();
    void updateEnabled();
    void doRead();
    void chooseWriteFile();
    void doCall();
    void doPrint();
    void saveMemory();
    [[nodiscard]] bool parseAddress(QLineEdit* edit, quint32& value) const;

    bool connected_ = false;
    bool busy_ = false;
    QByteArray memory_;
    quint32 memoryAddress_ = 0;

    QWidget* readCard_ = nullptr;
    QWidget* writeCard_ = nullptr;
    QWidget* controlCard_ = nullptr;

    QLineEdit* readAddress_ = nullptr;
    QLineEdit* readLength_ = nullptr;
    QPushButton* readButton_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPlainTextEdit* hexView_ = nullptr;
    QLabel* readInfo_ = nullptr;

    QLineEdit* writeAddress_ = nullptr;
    QLineEdit* writeFile_ = nullptr;
    QPushButton* writeBrowse_ = nullptr;
    QPushButton* writeButton_ = nullptr;

    QLineEdit* callAddress_ = nullptr;
    QPushButton* callButton_ = nullptr;
    QSpinBox* osdRow_ = nullptr;
    QSpinBox* osdColumn_ = nullptr;
    QLineEdit* osdText_ = nullptr;
    QCheckBox* flashBox_ = nullptr;
    QCheckBox* necBox_ = nullptr;
    QPushButton* printButton_ = nullptr;
};

} // namespace tpc2::gui
