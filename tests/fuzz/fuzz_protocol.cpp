// SPDX-License-Identifier: MIT
#include "core/Error.h"
#include "core/Protocol.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size)
{
    const std::string_view input(reinterpret_cast<const char*>(data), size);

    try {
        const auto encoded = tpc2::proto::encodeName(input);
        const auto decoded = tpc2::proto::decodeName(encoded.data());
        (void)tpc2::proto::describeNameConversion(
            input, std::span<const uint8_t>(encoded));
        (void)decoded;
    } catch (const tpc2::Error&) {
        // Invalid names are expected; they must fail without crashing.
    }

    (void)tpc2::proto::sanitizeLocalName(input);
    return 0;
}
