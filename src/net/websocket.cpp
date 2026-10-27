#include "net/websocket.h"

#include <array>
#include <cstdint>
#include <random>
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

void append64(std::string& output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xFFU));
    }
}

} // namespace

bool receiveWebSocket(SocketHandle socket, std::string& payload, int& opcode, bool masked) {
    std::array<char, 2> header{};
    if (!readExact(socket, header.data(), header.size())) return false;
    const auto first = static_cast<std::uint8_t>(header[0]);
    const auto second = static_cast<std::uint8_t>(header[1]);
    if ((first & 0x70U) != 0 || (first & 0x80U) == 0 ||
        (masked && (second & 0x80U) == 0) || (!masked && (second & 0x80U) != 0)) return false;
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
    // Full combat/fluid snapshots legitimately exceed 64 KiB. Keep a hard
    // allocation guard without disconnecting ordinary busy-world states.
    if (length > 8U * 1024U * 1024U) return false;
    std::array<char, 4> mask{};
    if (masked && !readExact(socket, mask.data(), mask.size())) return false;
    payload.resize(static_cast<std::size_t>(length));
    if (length > 0 && !readExact(socket, payload.data(), payload.size())) return false;
    if (masked) for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>(static_cast<std::uint8_t>(payload[i]) ^
                                      static_cast<std::uint8_t>(mask[i % 4U]));
    }
    return true;
}

bool sendFrame(SocketHandle socket, const std::string& payload, int opcode, bool masked) {
    std::string frame;
    frame.reserve(payload.size() + 14U);
    frame.push_back(static_cast<char>(0x80U | static_cast<std::uint8_t>(opcode & 0x0F)));
    const std::uint8_t maskBit = masked ? 0x80U : 0U;
    if (payload.size() <= 125U) {
        frame.push_back(static_cast<char>(maskBit | static_cast<std::uint8_t>(payload.size())));
    } else if (payload.size() <= 65535U) {
        frame.push_back(static_cast<char>(maskBit | 126U));
        append16(frame, static_cast<std::uint16_t>(payload.size()));
    } else {
        frame.push_back(static_cast<char>(maskBit | 127U));
        append64(frame, payload.size());
    }
    std::array<char, 4> mask{};
    if (masked) {
        std::random_device random;
        for (char& byte : mask) byte = static_cast<char>(random());
        frame.append(mask.data(), mask.size());
    }
    const std::size_t payloadOffset = frame.size();
    frame.append(payload);
    if (masked) for (std::size_t i = 0; i < payload.size(); ++i) {
        frame[payloadOffset + i] = static_cast<char>(static_cast<std::uint8_t>(payload[i]) ^
                                                    static_cast<std::uint8_t>(mask[i % 4U]));
    }
    return sendAll(socket, frame);
}

bool sendWebSocket(SocketHandle socket, const std::string& payload, int opcode) {
    return sendFrame(socket, payload, opcode, false);
}

bool sendWebSocketClient(SocketHandle socket, const std::string& payload, int opcode) {
    return sendFrame(socket, payload, opcode, true);
}

} // namespace net
