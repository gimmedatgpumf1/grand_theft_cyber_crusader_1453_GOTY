#include "net/platform.h"

#include <cstring>

#ifndef _WIN32
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace net {

bool initialize() {
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return true;
#endif
}

void cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

SocketHandle createListener(std::uint16_t port) {
    const SocketHandle listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalidSocket) return listener;
    int reuse = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listener, SOMAXCONN) != 0) {
        closeSocket(listener);
        return kInvalidSocket;
    }
    return listener;
}

SocketHandle acceptClient(SocketHandle listener) {
    return ::accept(listener, nullptr, nullptr);
}

SocketHandle connectClient(const std::string& host, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(port);
    if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses) != 0) return kInvalidSocket;
    SocketHandle result = kInvalidSocket;
    for (addrinfo* address = addresses; address; address = address->ai_next) {
        const SocketHandle socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket == kInvalidSocket) continue;
        if (::connect(socket, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0) {
            result = socket;
            break;
        }
        closeSocket(socket);
    }
    ::freeaddrinfo(addresses);
    return result;
}

void closeSocket(SocketHandle socket) {
    if (socket == kInvalidSocket) return;
#ifdef _WIN32
    ::closesocket(socket);
#else
    ::close(socket);
#endif
}

int readSome(SocketHandle socket, char* buffer, int size) {
#ifdef _WIN32
    return ::recv(socket, buffer, size, 0);
#else
    return static_cast<int>(::recv(socket, buffer, static_cast<std::size_t>(size), 0));
#endif
}

bool sendAll(SocketHandle socket, const char* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
#ifdef _WIN32
        const int result = ::send(socket, data + sent, static_cast<int>(size - sent), 0);
#else
        const int result = static_cast<int>(::send(socket, data + sent, size - sent, MSG_NOSIGNAL));
#endif
        if (result <= 0) return false;
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

bool sendAll(SocketHandle socket, const std::string& data) {
    return sendAll(socket, data.data(), data.size());
}

std::string lastError() {
#ifdef _WIN32
    return "Winsock error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

} // namespace net
