// SPDX-License-Identifier: MIT
#pragma once

#include "io/ByteTransport.h"

#include <QSerialPort>
#include <QString>

namespace tpc2 {

class QtSerialTransport final : public ByteTransport {
public:
    QtSerialTransport(QString portName, int baudRate);
    ~QtSerialTransport() override;

    void open() override;
    void close() override;
    [[nodiscard]] bool isOpen() const override;
    std::size_t read(uint8_t* out, std::size_t maxBytes, int timeoutMs) override;
    void write(const uint8_t* data, std::size_t size) override;
    [[nodiscard]] std::string description() const override;

private:
    QSerialPort port_;
    QString portName_;
    int baudRate_ = 115200;
};

} // namespace tpc2
