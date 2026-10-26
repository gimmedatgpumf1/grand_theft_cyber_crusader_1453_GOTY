#pragma once

#include "core/world.h"
#include "net/platform.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gtc {

class GameServer {
public:
    GameServer(std::uint16_t port, std::filesystem::path webRoot);
    int run();

private:
    struct Client {
        explicit Client(SocketHandle socketHandle) : socket(socketHandle) {}
        ~Client() { net::closeSocket(socket); }

        SocketHandle socket;
        std::uint32_t playerId = 0;
        std::mutex writer;
        std::atomic<bool> alive{true};
    };

    std::uint16_t port_;
    std::filesystem::path webRoot_;
    World world_;
    std::atomic<bool> running_{false};
    std::mutex clientsMutex_;
    std::vector<std::shared_ptr<Client>> clients_;
    std::thread simulationThread_;

    void handleConnection(SocketHandle socket);
    void handleMessage(const std::shared_ptr<Client>& client, const std::string& text);
    void serveHttp(SocketHandle socket, const std::string& method, const std::string& target);
    void simulationLoop();
    void broadcast(const std::string& message);
    void sendTo(const std::shared_ptr<Client>& client, const std::string& message, int opcode = 1);
    void removeClient(const std::shared_ptr<Client>& client);
};

} // namespace gtc
