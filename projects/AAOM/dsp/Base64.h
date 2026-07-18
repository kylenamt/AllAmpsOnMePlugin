#pragma once

// Minimal, dependency-free base64 decoder used to unpack the float32 tensor
// blobs stored in an aaom bundle. Kept header-only and JUCE-free so the whole
// MorphModel engine can be unit-tested outside the plugin.

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace aaom
{

// Decode a standard base64 string (RFC 4648, '+'/'/', optional '=' padding,
// whitespace ignored) into raw bytes. Throws std::runtime_error on invalid input.
inline std::vector<uint8_t> base64Decode(const std::string& in)
{
    static constexpr int8_t kInvalid = -1;
    auto lut = [](unsigned char c) -> int8_t {
        if (c >= 'A' && c <= 'Z') return static_cast<int8_t>(c - 'A');
        if (c >= 'a' && c <= 'z') return static_cast<int8_t>(c - 'a' + 26);
        if (c >= '0' && c <= '9') return static_cast<int8_t>(c - '0' + 52);
        if (c == '+') return 62;
        if (c == '/') return 63;
        return kInvalid;
    };

    std::vector<uint8_t> out;
    out.reserve((in.size() / 4) * 3 + 3);

    uint32_t buffer = 0;
    int bits = 0;
    for (unsigned char c : in)
    {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t')
            continue;
        const int8_t v = lut(c);
        if (v == kInvalid)
            throw std::runtime_error("base64Decode: invalid character in input");
        buffer = (buffer << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

// Decode a base64 blob of little-endian float32 values into a float vector.
// Assumes a little-endian host (x86 / ARM), matching the bundle spec.
inline std::vector<float> base64DecodeFloat32(const std::string& in)
{
    const std::vector<uint8_t> bytes = base64Decode(in);
    if (bytes.size() % sizeof(float) != 0)
        throw std::runtime_error("base64DecodeFloat32: byte count is not a multiple of 4");

    std::vector<float> out(bytes.size() / sizeof(float));
    if (!out.empty())
        std::memcpy(out.data(), bytes.data(), bytes.size());
    return out;
}

} // namespace aaom
