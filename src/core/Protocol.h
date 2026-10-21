// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tpc2::proto {

inline constexpr std::size_t kChunkSize = 32768;
inline constexpr std::size_t kNameField = 8;
inline constexpr std::size_t kExtField = 3;
inline constexpr std::size_t kFullNameField = kNameField + kExtField;

// The V64 uses a four byte little-endian size in TPC mode and a big-endian
// address/length pair in the GD6E extended mode. Keeping the helpers next to
// the protocol constants makes that difference difficult to get wrong.
inline void appendLe32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 24));
}

inline void appendBe32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

inline uint32_t readLe32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0])
        | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint32_t readBe32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24)
        | (static_cast<uint32_t>(p[1]) << 16)
        | (static_cast<uint32_t>(p[2]) << 8)
        | static_cast<uint32_t>(p[3]);
}

// Converts a user supplied name to the fixed 8.3 field transmitted by TPC.
// Throws Error(InvalidArgument) for names that contain path separators.
std::array<uint8_t, kFullNameField> encodeName(std::string_view name);

// Converts the exact 11 byte field coming from the device into "NAME.EXT".
std::string decodeName(const uint8_t* field, std::size_t length = kFullNameField);

// Returns just the filename component, strips characters that are illegal on
// Windows, avoids reserved device names, and never returns an empty string.
std::string sanitizeLocalName(std::string_view name);

// Returns a path in dir that does not exist yet, adding " (n)" before the
// extension when needed.
std::filesystem::path uniquePath(const std::filesystem::path& dir,
                                 const std::string& filename);

bool isBiosImageName(std::string_view name);

// A short human readable explanation of what encodeName changed, used by the
// GUI to show e.g. "renamed to FITNAME.BIN".
std::string describeNameConversion(std::string_view original,
                                   std::span<const uint8_t> encoded);

} // namespace tpc2::proto
