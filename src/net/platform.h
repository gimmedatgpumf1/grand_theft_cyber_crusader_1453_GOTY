#pragma once

#include <cstdint>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace net {

bool initialize();
void cleanup();
SocketHandle createListener(std::uint16_t port);
SocketHandle acceptClient(SocketHandle listener);
SocketHandle connectClient(const std::string& host, std::uint16_t port);
void closeSocket(SocketHandle socket);
int readSome(SocketHandle socket, char* buffer, int size);
bool waitReadable(SocketHandle socket, int milliseconds);
bool sendAll(SocketHandle socket, const char* data, std::size_t size);
bool sendAll(SocketHandle socket, const std::string& data);
std::string lastError();

} // namespace net
