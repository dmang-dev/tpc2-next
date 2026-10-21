// SPDX-License-Identifier: MIT
#include "gui/RemotePage.h"

#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

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

QString hexDump(const QByteArray& data, quint32 base, qsizetype limit)
{
    const qsizetype count = std::min<qsizetype>(data.size(), limit);
    QString out;
    out.reserve(count * 4);
    for (qsizetype offset = 0; offset < count; offset += 16) {
        out += QStringLiteral("%1  ").arg(
            (base + static_cast<quint32>(offset)), 8, 16, QLatin1Char('0')).toUpper();
        QString ascii;
        for (qsizetype i = 0; i < 16; ++i) {
            if (offset + i < count) {
                const unsigned char byte =
                    static_cast<unsigned char>(data[offset + i]);
                out += QStringLiteral("%1 ").arg(byte, 2, 16, QLatin1Char('0')).toUpper();
                ascii += (byte >= 0x20 && byte < 0x7F) ? QChar(byte) : QLatin1Char('.');
            } else {
                out += QStringLiteral("   ");
                ascii += QLatin1Char(' ');
            }
            if (i == 7)
                out += QLatin1Char(' ');
        }
        out += QStringLiteral(" |") + ascii + QStringLiteral("|\n");
    }
    if (data.size() > count)
        out += QStringLiteral("\n… %1 more bytes not shown …\n")
                   .arg(data.size() - count);
    return out;
}

} // namespace

RemotePage::RemotePage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("Page"));
    buildUi();
    updateEnabled();
}

void RemotePage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(14);

    auto* header = new QHBoxLayout;
    header->addWidget(makeLabel(QStringLiteral("Remote control"), "h1"));
    header->addSpacing(8);
    header->addWidget(makeLabel(QStringLiteral("GD6E"), "tag"));
    header->addStretch(1);
    header->addWidget(makeLabel(QStringLiteral("BIOS 1.30 or newer"), "sub"));
    root->addLayout(header);
    root->addWidget(makeLabel(
        QStringLiteral("Read and write V64 memory, call 6502 routines and draw on the "
                       "on-screen display. These commands use the extended GD6E protocol "
                       "introduced with BIOS 1.30."),
        "sub"));

    auto* columns = new QHBoxLayout;
    columns->setSpacing(14);

    auto* left = new QVBoxLayout;
    left->setSpacing(14);

    // Memory read ---------------------------------------------------------
    auto* readCard = makeCard();
    readCard_ = readCard;
    auto* readLayout = new QVBoxLayout(readCard);
    readLayout->setContentsMargins(18, 16, 18, 16);
    readLayout->setSpacing(9);

    auto* readHeader = new QHBoxLayout;
    readHeader->addWidget(makeLabel(QStringLiteral("Read memory"), "h2"));
    readHeader->addStretch(1);
    readHeader->addWidget(makeLabel(QStringLiteral("GD6ER"), "tag"));
    readLayout->addLayout(readHeader);

    auto* readForm = new QGridLayout;
    readForm->setHorizontalSpacing(8);
    readForm->setVerticalSpacing(6);
    readForm->addWidget(makeLabel(QStringLiteral("ADDRESS"), "tiny"), 0, 0);
    readForm->addWidget(makeLabel(QStringLiteral("LENGTH"), "tiny"), 0, 1);
    readAddress_ = new QLineEdit(QStringLiteral("0x00100000"));
    readAddress_->setPlaceholderText(QStringLiteral("0x00100000"));
    readLength_ = new QLineEdit(QStringLiteral("0x00010000"));
    readLength_->setPlaceholderText(QStringLiteral("0x00010000"));
    readForm->addWidget(readAddress_, 1, 0);
    readForm->addWidget(readLength_, 1, 1);
    readButton_ = new QPushButton(QStringLiteral("Read"));
    readButton_->setProperty("accent", true);
    saveButton_ = new QPushButton(QStringLiteral("Save…"));
    saveButton_->setEnabled(false);
    readForm->addWidget(readButton_, 1, 2);
    readForm->addWidget(saveButton_, 1, 3);
    readLayout->addLayout(readForm);

    readInfo_ = makeLabel(QStringLiteral("Nothing read yet"), "tiny");
    readLayout->addWidget(readInfo_);

    hexView_ = new QPlainTextEdit;
    hexView_->setReadOnly(true);
    hexView_->setPlaceholderText(
        QStringLiteral("The hex dump appears here.\n"
                       "Addresses 0x00000000–0x01FFFFFF are DRAM; 0x80000000–0x8000FFFF are the 6502 CPU space."));
    hexView_->setMinimumHeight(250);
    readLayout->addWidget(hexView_, 1);
    left->addWidget(readCard, 3);

    // Memory write --------------------------------------------------------
    auto* writeCard = makeCard();
    writeCard_ = writeCard;
    auto* writeLayout = new QVBoxLayout(writeCard);
    writeLayout->setContentsMargins(18, 16, 18, 16);
    writeLayout->setSpacing(9);
    auto* writeHeader = new QHBoxLayout;
    writeHeader->addWidget(makeLabel(QStringLiteral("Write memory"), "h2"));
    writeHeader->addStretch(1);
    writeHeader->addWidget(makeLabel(QStringLiteral("GD6EW"), "tag"));
    writeLayout->addLayout(writeHeader);

    auto* writeForm = new QGridLayout;
    writeForm->setHorizontalSpacing(8);
    writeForm->setVerticalSpacing(6);
    writeForm->addWidget(makeLabel(QStringLiteral("ADDRESS"), "tiny"), 0, 0, 1, 2);
    writeAddress_ = new QLineEdit(QStringLiteral("0x00100000"));
    writeAddress_->setPlaceholderText(QStringLiteral("0x00100000"));
    writeForm->addWidget(writeAddress_, 1, 0, 1, 2);
    writeForm->addWidget(makeLabel(QStringLiteral("FILE"), "tiny"), 0, 2);
    writeFile_ = new QLineEdit;
    writeFile_->setPlaceholderText(QStringLiteral("Choose a binary file"));
    writeBrowse_ = new QPushButton(QStringLiteral("Browse"));
    writeButton_ = new QPushButton(QStringLiteral("Write"));
    writeButton_->setProperty("accent", true);
    writeForm->addWidget(writeFile_, 1, 2);
    writeForm->addWidget(writeBrowse_, 1, 3);
    writeForm->addWidget(writeButton_, 1, 4);
    writeLayout->addLayout(writeForm);
    writeLayout->addWidget(makeLabel(
        QStringLiteral("CPU RAM writes are limited by the V64 BIOS to 0x0000–0x011F "
                       "and 0x0200–0x07FF; DRAM writes go to 0x00000000–0x01FFFFFF."),
        "tiny"));
    left->addWidget(writeCard, 0);

    columns->addLayout(left, 3);

    // CPU / OSD -----------------------------------------------------------
    auto* right = new QVBoxLayout;
    right->setSpacing(14);

    auto* controlCard = makeCard(true);
    controlCard_ = controlCard;
    auto* controlLayout = new QVBoxLayout(controlCard);
    controlLayout->setContentsMargins(18, 16, 18, 16);
    controlLayout->setSpacing(10);
    auto* controlHeader = new QHBoxLayout;
    controlHeader->addWidget(makeLabel(QStringLiteral("CPU & OSD"), "h2"));
    controlHeader->addStretch(1);
    controlHeader->addWidget(makeLabel(QStringLiteral("GD6EC / GD6EP"), "tag"));
    controlLayout->addLayout(controlHeader);

    controlLayout->addWidget(makeLabel(QStringLiteral("CALL ROUTINE"), "tiny"));
    auto* callRow = new QHBoxLayout;
    callAddress_ = new QLineEdit(QStringLiteral("0xFFE3"));
    callAddress_->setPlaceholderText(QStringLiteral("0xFFE3"));
    callButton_ = new QPushButton(QStringLiteral("Call"));
    callRow->addWidget(callAddress_, 1);
    callRow->addWidget(callButton_);
    controlLayout->addLayout(callRow);
    controlLayout->addWidget(makeLabel(
        QStringLiteral("FFE3 clears the V64 display. FFEC redraws the game name."),
        "tiny"));
    controlLayout->addSpacing(8);

    controlLayout->addWidget(makeLabel(QStringLiteral("ON-SCREEN DISPLAY"), "tiny"));
    auto* osdGrid = new QGridLayout;
    osdGrid->setHorizontalSpacing(8);
    osdGrid->setVerticalSpacing(6);
    osdGrid->addWidget(makeLabel(QStringLiteral("ROW"), "tiny"), 0, 0, 1, 2);
    osdGrid->addWidget(makeLabel(QStringLiteral("COL"), "tiny"), 0, 2);
    osdRow_ = new QSpinBox;
    osdRow_->setRange(0, 10);
    osdRow_->setValue(1);
    osdColumn_ = new QSpinBox;
    osdColumn_->setRange(0, 23);
    osdColumn_->setValue(2);
    osdGrid->addWidget(osdRow_, 1, 0, 1, 2);
    osdGrid->addWidget(osdColumn_, 1, 2);
    osdText_ = new QLineEdit(QStringLiteral("TPC2 NEXT"));
    osdText_->setPlaceholderText(QStringLiteral("Text to display"));
    osdGrid->addWidget(osdText_, 2, 0, 1, 3);
    printButton_ = new QPushButton(QStringLiteral("Send text"));
    printButton_->setProperty("accent", true);
    osdGrid->addWidget(printButton_, 2, 3);

    auto* osdFlags = new QHBoxLayout;
    osdFlags->setSpacing(14);
    flashBox_ = new QCheckBox(QStringLiteral("Flashing text"));
    flashBox_->setToolTip(QStringLiteral(
        "V64IO2 D7 flag: blink the characters on the V64 OSD."));
    necBox_ = new QCheckBox(QStringLiteral("NEC6450 font"));
    necBox_->setToolTip(QStringLiteral(
        "V64IO2 D6 flag: interpret characters as NEC6450 OSD codes."));
    osdFlags->addWidget(flashBox_);
    osdFlags->addWidget(necBox_);
    osdFlags->addStretch(1);
    osdGrid->addLayout(osdFlags, 3, 0, 1, 4);

    controlLayout->addLayout(osdGrid);
    controlLayout->addWidget(makeLabel(
        QStringLiteral("The V64 OSD is 11 rows × 24 columns. Printable ASCII is "
                       "sent; flash and NEC6450 flags match V64IO2."),
        "tiny"));
    controlLayout->addStretch(1);
    right->addWidget(controlCard, 1);

    auto* noteCard = makeCard();
    auto* noteLayout = new QVBoxLayout(noteCard);
    noteLayout->setContentsMargins(18, 16, 18, 16);
    noteLayout->setSpacing(7);
    noteLayout->addWidget(makeLabel(QStringLiteral("Good to know"), "h2"));
    auto* note = makeLabel(
        QStringLiteral(
            "•  Extended commands require BIOS 1.30+.\n"
            "•  DRAM is 0x00000000–0x01FFFFFF (up to 32 MB).\n"
            "•  CPU space is 0x80000000–0x8000FFFF.\n"
            "•  Reads pause the 6502 while the V64 streams data.\n"
            "•  Use the demo backend to try these controls safely."),
        "sub");
    note->setWordWrap(true);
    noteLayout->addWidget(note);
    right->addWidget(noteCard, 0);

    columns->addLayout(right, 2);
    root->addLayout(columns, 1);

    connect(readButton_, &QPushButton::clicked, this, &RemotePage::doRead);
    connect(saveButton_, &QPushButton::clicked, this, &RemotePage::saveMemory);
    connect(writeBrowse_, &QPushButton::clicked, this, &RemotePage::chooseWriteFile);
    connect(writeButton_, &QPushButton::clicked, this, [this] {
        quint32 address = 0;
        if (!parseAddress(writeAddress_, address))
            return;
        if (writeFile_->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("No file"),
                                 QStringLiteral("Choose a file to write."));
            return;
        }
        const QFileInfo info(writeFile_->text());
        if (!info.isFile()) {
            QMessageBox::warning(this, QStringLiteral("No file"),
                                 QStringLiteral("The selected file does not exist."));
            return;
        }
        emit writeRequested(address, writeFile_->text());
    });
    connect(callButton_, &QPushButton::clicked, this, &RemotePage::doCall);
    connect(printButton_, &QPushButton::clicked, this, &RemotePage::doPrint);
}

void RemotePage::setConnected(bool connected)
{
    connected_ = connected;
    updateEnabled();
}

void RemotePage::setBusy(bool busy)
{
    busy_ = busy;
    updateEnabled();
}

void RemotePage::updateEnabled()
{
    const bool enabled = connected_ && !busy_;
    for (QWidget* widget : { static_cast<QWidget*>(readCard_),
                             static_cast<QWidget*>(writeCard_),
                             static_cast<QWidget*>(controlCard_) }) {
        if (widget)
            widget->setEnabled(enabled);
    }
    if (saveButton_)
        saveButton_->setEnabled(enabled && !memory_.isEmpty());
    if (hexView_)
        hexView_->setEnabled(true);
}

bool RemotePage::parseAddress(QLineEdit* edit, quint32& value) const
{
    if (!parseNumber(edit->text(), value)) {
        QMessageBox::warning(const_cast<RemotePage*>(this),
                             QStringLiteral("Invalid number"),
                             QStringLiteral("Enter a decimal value or a 0x-prefixed "
                                            "hexadecimal value."));
        return false;
    }
    return true;
}

void RemotePage::doRead()
{
    quint32 address = 0;
    quint32 length = 0;
    if (!parseAddress(readAddress_, address) || !parseAddress(readLength_, length))
        return;
    constexpr quint32 kMaxRead = 16u * 1024u * 1024u;
    if (length == 0) {
        QMessageBox::warning(this, QStringLiteral("Invalid length"),
                             QStringLiteral("Length must be greater than zero."));
        return;
    }
    if (length > kMaxRead) {
        QMessageBox::warning(this, QStringLiteral("Length too large"),
                             QStringLiteral("The GUI limits a single read to 16 MB."));
        return;
    }
    if (static_cast<quint64>(address) + length > 0x1'0000'0000ull) {
        QMessageBox::warning(this, QStringLiteral("Invalid range"),
                             QStringLiteral("The address range overflows 32 bits."));
        return;
    }
    saveButton_->setEnabled(false);
    readInfo_->setText(QStringLiteral("Reading %1 bytes from 0x%2…")
                           .arg(length)
                           .arg(address, 8, 16, QLatin1Char('0')));
    emit readRequested(address, length);
}

void RemotePage::saveMemory()
{
    if (memory_.isEmpty())
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save memory dump"), QStringLiteral("memory.bin"),
        QStringLiteral("Binary files (*.bin);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("Cannot save"),
                             file.errorString());
        return;
    }
    file.write(memory_);
}

void RemotePage::chooseWriteFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Choose a binary file"), {},
        QStringLiteral("Binary files (*.bin *.rom *.z64 *.v64);;All files (*)"));
    if (!path.isEmpty())
        writeFile_->setText(path);
}

void RemotePage::doCall()
{
    quint32 address = 0;
    if (!parseAddress(callAddress_, address))
        return;
    if (address > 0xFFFF) {
        QMessageBox::warning(this, QStringLiteral("Invalid address"),
                             QStringLiteral("The 6502 call address must be 0x0000–0xFFFF."));
        return;
    }
    emit callRequested(address);
}

void RemotePage::doPrint()
{
    const QString text = osdText_->text();
    if (text.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("No text"),
                             QStringLiteral("Enter the text to display."));
        return;
    }
    emit printRequested(osdRow_->value(), osdColumn_->value(), text,
                        flashBox_->isChecked(), necBox_->isChecked());
}

void RemotePage::setMemory(const QByteArray& bytes, quint32 address)
{
    memory_ = bytes;
    memoryAddress_ = address;
    constexpr qsizetype kPreview = 64 * 1024;
    hexView_->setPlainText(hexDump(bytes, address, kPreview));
    readInfo_->setText(QStringLiteral("%1 bytes at 0x%2%3")
                           .arg(bytes.size())
                           .arg(address, 8, 16, QLatin1Char('0'))
                           .arg(bytes.size() > kPreview
                                    ? QStringLiteral("  ·  preview limited to 64 KB")
                                    : QString()));
    saveButton_->setEnabled(connected_ && !busy_ && !bytes.isEmpty());
}

} // namespace tpc2::gui
