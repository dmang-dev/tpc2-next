// SPDX-License-Identifier: MIT
#include "core/Error.h"
#include "io/BridgeFrame.h"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size)
{
    tpc2::bridge::Decoder decoder;
    try {
        (void)decoder.feed(std::span<const uint8_t>(data, size));
    } catch (const tpc2::Error&) {
        // Invalid frames are expected; the decoder must reject them cleanly.
    }
    return 0;
}
