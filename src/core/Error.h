// SPDX-License-Identifier: MIT
#pragma once

#include <stdexcept>
#include <string>

namespace tpc2 {

enum class ErrorCode {
    Ok = 0,
    NotConnected,
    Timeout,
    Protocol,
    Io,
    Cancelled,
    Unsupported,
    InvalidArgument,
    DeviceRejected,
    Checksum,
};

class Error final : public std::runtime_error {
public:
    Error(ErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

[[nodiscard]] inline const char* errorCodeName(ErrorCode code) noexcept
{
    switch (code) {
    case ErrorCode::Ok: return "ok";
    case ErrorCode::NotConnected: return "not-connected";
    case ErrorCode::Timeout: return "timeout";
    case ErrorCode::Protocol: return "protocol";
    case ErrorCode::Io: return "io";
    case ErrorCode::Cancelled: return "cancelled";
    case ErrorCode::Unsupported: return "unsupported";
    case ErrorCode::InvalidArgument: return "invalid-argument";
    case ErrorCode::DeviceRejected: return "device-rejected";
    case ErrorCode::Checksum: return "checksum";
    }
    return "unknown";
}

} // namespace tpc2
