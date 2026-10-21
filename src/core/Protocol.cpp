// SPDX-License-Identifier: MIT
#include "core/Protocol.h"

#include "core/Error.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_set>

namespace tpc2::proto {
namespace {

bool isInvalid83(char c)
{
    const unsigned char u = static_cast<unsigned char>(c);
    if (u <= 0x20 || u > 0x7e)
        return true;
    return std::strchr("<>:\"/\\|?*+,;=[]", c) != nullptr;
}

std::string upperAscii(std::string s)
{
    for (char& c : s) {
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    }
    return s;
}

std::string trimField(const uint8_t* p, std::size_t n)
{
    std::size_t end = n;
    while (end > 0 && (p[end - 1] == ' ' || p[end - 1] == 0))
        --end;
    std::string out;
    out.reserve(end);
    for (std::size_t i = 0; i < end; ++i) {
        const unsigned char c = p[i];
        out.push_back(c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '_');
    }
    return out;
}

bool isReservedStem(std::string stem)
{
    stem = upperAscii(std::move(stem));
    static const std::unordered_set<std::string> reserved = {
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
        "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2",
        "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };
    return reserved.contains(stem);
}

} // namespace

std::array<uint8_t, kFullNameField> encodeName(std::string_view name)
{
    if (name.empty())
        throw Error(ErrorCode::InvalidArgument, "File name is empty");
    if (name.find_first_of("/\\") != std::string_view::npos)
        throw Error(ErrorCode::InvalidArgument,
                    "File name must not contain a path: " + std::string(name));

    std::string_view stem = name;
    std::string_view ext;
    const auto dot = name.find_last_of('.');
    if (dot != std::string_view::npos && dot != 0) {
        stem = name.substr(0, dot);
        ext = name.substr(dot + 1);
    }

    std::string cleanStem;
    std::string cleanExt;
    for (char c : stem)
        cleanStem.push_back(isInvalid83(c) ? '_' : c);
    for (char c : ext)
        cleanExt.push_back(isInvalid83(c) ? '_' : c);

    cleanStem = upperAscii(std::move(cleanStem));
    cleanExt = upperAscii(std::move(cleanExt));

    if (cleanStem.empty())
        cleanStem = "FILE";
    if (cleanStem.size() > kNameField)
        cleanStem.resize(kNameField);
    if (cleanExt.size() > kExtField)
        cleanExt.resize(kExtField);

    std::array<uint8_t, kFullNameField> out {};
    out.fill(' ');
    std::copy(cleanStem.begin(), cleanStem.end(), out.begin());
    std::copy(cleanExt.begin(), cleanExt.end(), out.begin() + kNameField);
    return out;
}

std::string decodeName(const uint8_t* field, std::size_t length)
{
    if (length == 0)
        return {};
    std::string base = trimField(field, std::min<std::size_t>(length, kNameField));
    if (base.empty())
        base = "UNTITLED";
    std::string ext;
    if (length > kNameField)
        ext = trimField(field + kNameField, length - kNameField);
    return ext.empty() ? base : base + "." + ext;
}

std::string sanitizeLocalName(std::string_view name)
{
    std::string s(name);
    const auto slash = s.find_last_of("/\\");
    if (slash != std::string::npos)
        s.erase(0, slash + 1);

    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f)
            out.push_back('_');
        else if (std::strchr("<>:\"/\\|?*", c) != nullptr)
            out.push_back('_');
        else
            out.push_back(c);
    }

    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    while (!out.empty() && (out.front() == ' ' || out.front() == '.'))
        out.erase(out.begin());

    if (out.empty() || out == "." || out == "..")
        out = "UNTITLED";

    const auto dot = out.find_last_of('.');
    const std::string stem = dot == std::string::npos ? out : out.substr(0, dot);
    if (isReservedStem(stem))
        out.insert(out.begin(), '_');

    if (out.size() > 200)
        out.resize(200);
    return out;
}

std::filesystem::path uniquePath(const std::filesystem::path& dir,
                                 const std::string& filename)
{
    const std::string safe = sanitizeLocalName(filename);
    auto candidate = dir / safe;
    if (!std::filesystem::exists(candidate))
        return candidate;

    const std::filesystem::path p(safe);
    const std::string stem = p.stem().string();
    const std::string ext = p.extension().string();
    for (int i = 1; i < 10000; ++i) {
        candidate = dir / (stem + " (" + std::to_string(i) + ")" + ext);
        if (!std::filesystem::exists(candidate))
            return candidate;
    }
    throw Error(ErrorCode::Io, "Could not find a free file name in " + dir.string());
}

bool isBiosImageName(std::string_view name)
{
    const std::string upper = upperAscii(std::string(name));
    if (upper.size() < 10 || upper.rfind("V64_VER", 0) != 0)
        return false;
    const std::size_t dot = upper.find('.', 7);
    if (dot == std::string::npos || dot != 8)
        return false;
    if (upper.size() != dot + 3)
        return false;
    return std::isdigit(static_cast<unsigned char>(upper[7]))
        && std::isdigit(static_cast<unsigned char>(upper[dot + 1]))
        && std::isdigit(static_cast<unsigned char>(upper[dot + 2]));
}

std::string describeNameConversion(std::string_view original,
                                   std::span<const uint8_t> encoded)
{
    const std::string converted = decodeName(encoded.data(), encoded.size());
    if (upperAscii(std::string(original)) == converted)
        return converted;
    return converted;
}

} // namespace tpc2::proto
