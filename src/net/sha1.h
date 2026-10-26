#pragma once

#include <string>

namespace net {

// Used only for the WebSocket opening handshake, not for password security.
std::string webSocketAccept(const std::string& clientKey);

} // namespace net
