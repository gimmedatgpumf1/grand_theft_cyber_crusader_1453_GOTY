#include "net/websocket.h"

#include <array>
#include <cstdint>
#include <vector>

namespace net {
namespace {

bool readExact(SocketHandle socket, char* output, std::size_t size) {
    std::size_t received = 0;
    while (received < size) {
        const int result = readSome(socket, output + received, static_cast<int>(size - received));
        if (result <= 0) return false;
        received += static_cast<std::size_t>(result);
    }
    return true;
}

void append16(std::string& output, std::uint16_t value) {
    output.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<char>(value & 0xFFU));
}

} // namespace

bool receiveWebSocket(SocketHandle socket, std::string& payload, int& opcode) {
    std::array<char, 2> header{};
    if (!readExact(socket, header.data(), header.size())) return false;
    const auto first = static_cast<std::uint8_t>(header[0]);
    const auto second = static_cast<std::uint8_t>(header[1]);
    if ((first & 0x70U) != 0 || (first & 0x80U) == 0 || (second & 0x80U) == 0) return false;
    opcode = first & 0x0FU;
    if (opcode == 0 || opcode > 10) return false;
    std::uint64_t length = second & 0x7FU;
    if (length == 126U) {
        std::array<char, 2> extended{};
        if (!readExact(socket, extended.data(), extended.size())) return false;
        length = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(extended[0])) << 8U) |
                 static_cast<std::uint8_t>(extended[1]);
    } else if (length == 127U) {
        std::array<char, 8> extended{};
        if (!readExact(socket, extended.data(), extended.size())) return false;
        length = 0;
        for (const char byte : extended) {
            length = (length << 8U) | static_cast<std::uint8_t>(byte);
        }
    }
    if (length > 65536U) return false;
    std::array<char, 4> mask{};
    if (!readExact(socket, mask.data(), mask.size())) return false;
    payload.resize(static_cast<std::size_t>(length));
    if (length > 0 && !readExact(socket, payload.data(), payload.size())) return false;
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>(static_cast<std::uint8_t>(payload[i]) ^
                                      static_cast<std::uint8_t>(mask[i % 4U]));
    }
    return true;
}

bool sendWebSocket(SocketHandle socket, const std::string& payload, int opcode) {
    std::string frame;
    frame.reserve(payload.size() + 10U);
    frame.push_back(static_cast<char>(0x80U | (opcode & 0x0F)));
    if (payload.size() < 126U) {
        frame.push_back(static_cast<char>(payload.size()));
    } else if (payload.size() <= 65535U) {
        frame.push_back(static_cast<char>(126));
        append16(frame, static_cast<std::uint16_t>(payload.size()));
    } else {
        frame.push_back(static_cast<char>(127));
        const std::uint64_t length = payload.size();
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<char>((length >> shift) & 0xFFU));
        }
    }
    frame += payload;
    return sendAll(socket, frame);
}

} // namespace net
