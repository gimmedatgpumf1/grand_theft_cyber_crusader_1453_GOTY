#pragma once

#include <cstdint>
#include <string>

namespace gtc::native {

struct InteractiveOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 1453;
    std::string name = "Native Citizen";
    std::string root = ".";
};

int runInteractive(const InteractiveOptions& options);

} // namespace gtc::native
