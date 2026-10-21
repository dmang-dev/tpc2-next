// SPDX-License-Identifier: MIT
#include "io/QtSerialTransport.h"

#include "core/Error.h"

#include <QSerialPortInfo>

namespace tpc2 {

QtSerialTransport::QtSerialTransport(QString portName, int baudRate)
    : portName_(std::move(portName)), baudRate_(baudRate)
{
}

QtSerialTransport::~QtSerialTransport()
{
    close();
}

void QtSerialTransport::open()
{
    if (port_.isOpen())
        return;
    port_.setPortName(portName_);
    port_.setBaudRate(baudRate_);
    port_.setDataBits(QSerialPort::Data8);
    port_.setParity(QSerialPort::NoParity);
    port_.setStopBits(QSerialPort::OneStop);
    port_.setFlowControl(QSerialPort::NoFlowControl);
    if (!port_.open(QIODevice::ReadWrite)) {
        throw Error(ErrorCode::Io,
                    "Cannot open " + portName_.toStdString() + ": "
                        + port_.errorString().toStdString());
    }
    port_.clear(QSerialPort::AllDirections);
}

void QtSerialTransport::close()
{
    if (port_.isOpen())
        port_.close();
}

bool QtSerialTransport::isOpen() const
{
    return port_.isOpen();
}

std::size_t QtSerialTransport::read(uint8_t* out, std::size_t maxBytes,
                                    int timeoutMs)
{
    if (!port_.isOpen())
        throw Error(ErrorCode::NotConnected, "Serial port is not open");
    if (maxBytes == 0)
        return 0;
    if (port_.bytesAvailable() == 0 && !port_.waitForReadyRead(timeoutMs)) {
        if (port_.error() == QSerialPort::ResourceError)
            throw Error(ErrorCode::Io, "Serial port disconnected");
        return 0;
    }
    const QByteArray data = port_.read(static_cast<qint64>(maxBytes));
    if (data.isEmpty())
        return 0;
    std::memcpy(out, data.constData(), static_cast<std::size_t>(data.size()));
    return static_cast<std::size_t>(data.size());
}

void QtSerialTransport::write(const uint8_t* data, std::size_t size)
{
    if (!port_.isOpen())
        throw Error(ErrorCode::NotConnected, "Serial port is not open");

    std::size_t offset = 0;
    while (offset < size) {
        const qint64 written = port_.write(
            reinterpret_cast<const char*>(data + offset),
            static_cast<qint64>(size - offset));
        if (written < 0)
            throw Error(ErrorCode::Io,
                        "Serial write failed: " + port_.errorString().toStdString());
        offset += static_cast<std::size_t>(written);
        if (!port_.waitForBytesWritten(3000)) {
            throw Error(ErrorCode::Timeout,
                        "Serial write timed out: " + port_.errorString().toStdString());
        }
    }
}

std::string QtSerialTransport::description() const
{
    return portName_.toStdString() + " @ " + std::to_string(baudRate_);
}

} // namespace tpc2
