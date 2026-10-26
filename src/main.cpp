#include "server/game_server.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::uint16_t port = 1453;
    std::filesystem::path webRoot = std::filesystem::current_path() / "web";
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--port" && i + 1 < argc) {
            const int requested = std::stoi(argv[++i]);
            if (requested < 1 || requested > 65535) {
                std::cerr << "Port must be between 1 and 65535.\n";
                return 2;
            }
            port = static_cast<std::uint16_t>(requested);
        } else if (argument == "--web" && i + 1 < argc) {
            webRoot = argv[++i];
        } else if (argument == "--help") {
            std::cout << "Usage: gtc1453 [--port 1453] [--web ./web]\n";
            return 0;
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            return 2;
        }
    }
    gtc::GameServer server(port, webRoot);
    return server.run();
}
