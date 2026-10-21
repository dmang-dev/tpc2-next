// SPDX-License-Identifier: MIT
#include "platform/ParallelPort.h"

#include "core/Error.h"

namespace tpc2 {

std::vector<PortDescriptor> ParallelPort::enumerate()
{
    return {};
}

std::unique_ptr<ParallelPort> ParallelPort::create()
{
    throw Error(ErrorCode::Unsupported,
                "macOS has no parallel port. Use the USB bridge backend.");
}

bool ParallelPort::supported()
{
    return false;
}

} // namespace tpc2
