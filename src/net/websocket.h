#pragma once

#include "net/platform.h"

#include <string>

namespace net {

bool receiveWebSocket(SocketHandle socket, std::string& payload, int& opcode);
bool sendWebSocket(SocketHandle socket, const std::string& payload, int opcode = 1);

} // namespace net
