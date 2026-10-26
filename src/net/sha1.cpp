#include "net/sha1.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace net {
namespace {

std::uint32_t rotate(std::uint32_t value, unsigned int bits) {
    return (value << bits) | (value >> (32U - bits));
}

std::array<std::uint8_t, 20> sha1(const std::string& input) {
    std::vector<std::uint8_t> data(input.begin(), input.end());
    const std::uint64_t bitLength = static_cast<std::uint64_t>(data.size()) * 8U;
    data.push_back(0x80U);
    while ((data.size() % 64U) != 56U) data.push_back(0U);
    for (int shift = 56; shift >= 0; shift -= 8) {
        data.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xFFU));
    }

    std::uint32_t h0 = 0x67452301U;
    std::uint32_t h1 = 0xEFCDAB89U;
    std::uint32_t h2 = 0x98BADCFEU;
    std::uint32_t h3 = 0x10325476U;
    std::uint32_t h4 = 0xC3D2E1F0U;
    for (std::size_t offset = 0; offset < data.size(); offset += 64U) {
        std::uint32_t words[80]{};
        for (int i = 0; i < 16; ++i) {
            const std::size_t at = offset + static_cast<std::size_t>(i * 4);
            words[i] = (static_cast<std::uint32_t>(data[at]) << 24U) |
                       (static_cast<std::uint32_t>(data[at + 1]) << 16U) |
                       (static_cast<std::uint32_t>(data[at + 2]) << 8U) |
                       static_cast<std::uint32_t>(data[at + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            words[i] = rotate(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
        }
        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;
        for (int i = 0; i < 80; ++i) {
            std::uint32_t function = 0;
            std::uint32_t constant = 0;
            if (i < 20) {
                function = (b & c) | ((~b) & d);
                constant = 0x5A827999U;
            } else if (i < 40) {
                function = b ^ c ^ d;
                constant = 0x6ED9EBA1U;
            } else if (i < 60) {
                function = (b & c) | (b & d) | (c & d);
                constant = 0x8F1BBCDCU;
            } else {
                function = b ^ c ^ d;
                constant = 0xCA62C1D6U;
            }
            const std::uint32_t temporary = rotate(a, 5) + function + e + constant + words[i];
            e = d;
            d = c;
            c = rotate(b, 30);
            b = a;
            a = temporary;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<std::uint8_t, 20> digest{};
    const std::uint32_t hashes[] = {h0, h1, h2, h3, h4};
    for (std::size_t i = 0; i < 5; ++i) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            digest[i * 4 + byte] = static_cast<std::uint8_t>((hashes[i] >> (24U - byte * 8U)) & 0xFFU);
        }
    }
    return digest;
}

std::string base64(const std::uint8_t* data, std::size_t size) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((size + 2U) / 3U) * 4U);
    for (std::size_t i = 0; i < size; i += 3U) {
        const std::uint32_t a = data[i];
        const std::uint32_t b = i + 1U < size ? data[i + 1U] : 0U;
        const std::uint32_t c = i + 2U < size ? data[i + 2U] : 0U;
        const std::uint32_t value = (a << 16U) | (b << 8U) | c;
        output.push_back(alphabet[(value >> 18U) & 63U]);
        output.push_back(alphabet[(value >> 12U) & 63U]);
        output.push_back(i + 1U < size ? alphabet[(value >> 6U) & 63U] : '=');
        output.push_back(i + 2U < size ? alphabet[value & 63U] : '=');
    }
    return output;
}

} // namespace

std::string webSocketAccept(const std::string& clientKey) {
    const auto digest = sha1(clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    return base64(digest.data(), digest.size());
}

} // namespace net
