#include "server/game_server.h"

#include "core/json.h"
#include "net/sha1.h"
#include "net/websocket.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>

namespace gtc {
namespace {

struct HttpRequest {
    std::string method;
    std::string target;
    std::map<std::string, std::string> headers;
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) return "";
    const std::size_t last = value.find_last_not_of(" \t\r");
    return value.substr(first, last - first + 1);
}

bool readHttp(SocketHandle socket, HttpRequest& request) {
    std::string data;
    char buffer[2048];
    while (data.find("\r\n\r\n") == std::string::npos) {
        if (data.size() > 16384U) return false;
        const int count = net::readSome(socket, buffer, sizeof(buffer));
        if (count <= 0) return false;
        data.append(buffer, static_cast<std::size_t>(count));
    }
    std::istringstream input(data);
    std::string line;
    if (!std::getline(input, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream first(line);
    std::string version;
    if (!(first >> request.method >> request.target >> version)) return false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos) {
            request.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
        }
    }
    return true;
}

std::string mimeType(const std::filesystem::path& path) {
    const std::string extension = lower(path.extension().string());
    if (extension == ".html") return "text/html; charset=utf-8";
    if (extension == ".css") return "text/css; charset=utf-8";
    if (extension == ".js" || extension == ".mjs") return "text/javascript; charset=utf-8";
    if (extension == ".json") return "application/json; charset=utf-8";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".wav") return "audio/wav";
    return "application/octet-stream";
}

void httpResponse(SocketHandle socket, int status, const std::string& statusText,
                  const std::string& contentType, const std::string& body) {
    std::ostringstream header;
    header << "HTTP/1.1 " << status << ' ' << statusText << "\r\n"
           << "Content-Type: " << contentType << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n"
           << "X-Content-Type-Options: nosniff\r\n"
           << "Cache-Control: no-cache\r\n\r\n";
    net::sendAll(socket, header.str());
    if (!body.empty()) net::sendAll(socket, body);
}

bool decodeUrl(const std::string& source, std::string& output) {
    output.clear();
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] != '%') {
            output.push_back(source[i]);
            continue;
        }
        if (i + 2 >= source.size()) return false;
        const auto hex = [](char value) -> int {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'a' && value <= 'f') return value - 'a' + 10;
            if (value >= 'A' && value <= 'F') return value - 'A' + 10;
            return -1;
        };
        const int high = hex(source[i + 1]);
        const int low = hex(source[i + 2]);
        if (high < 0 || low < 0) return false;
        output.push_back(static_cast<char>((high << 4) | low));
        i += 2;
    }
    return true;
}

bool containsParentTraversal(const std::filesystem::path& path) {
    for (const auto& part : path) {
        if (part == "..") return true;
    }
    return false;
}

} // namespace

GameServer::GameServer(std::uint16_t port, std::filesystem::path webRoot)
    : port_(port), webRoot_(std::move(webRoot)) {}

int GameServer::run() {
    if (!net::initialize()) {
        std::cerr << "Could not initialize networking.\n";
        return 1;
    }
    std::error_code error;
    webRoot_ = std::filesystem::canonical(webRoot_, error);
    if (error || !std::filesystem::is_directory(webRoot_)) {
        std::cerr << "Web root not found. Run the game from the repository root.\n";
        return 1;
    }
    const SocketHandle listener = net::createListener(port_);
    if (listener == kInvalidSocket) {
        std::cerr << "Could not bind port " << port_ << ": " << net::lastError() << '\n';
        return 1;
    }
    world_.addCharacter("NPC: Clogsworth", true);
    running_ = true;
    simulationThread_ = std::thread(&GameServer::simulationLoop, this);
    std::cout << "GRAND THEFT CYBER CRUSADER 1453\n"
              << "Open http://localhost:" << port_ << " (or your LAN address)\n"
              << "Simulation thread online. Toilet pressure nominal.\n" << std::flush;
    while (running_) {
        const SocketHandle socket = net::acceptClient(listener);
        if (socket == kInvalidSocket) {
            if (running_) std::cerr << "Accept failed: " << net::lastError() << '\n';
            continue;
        }
        std::thread(&GameServer::handleConnection, this, socket).detach();
    }
    net::closeSocket(listener);
    if (simulationThread_.joinable()) simulationThread_.join();
    net::cleanup();
    return 0;
}

void GameServer::handleConnection(SocketHandle socket) {
    HttpRequest request;
    if (!readHttp(socket, request)) {
        net::closeSocket(socket);
        return;
    }
    const auto upgrade = request.headers.find("upgrade");
    if (upgrade == request.headers.end() || lower(upgrade->second) != "websocket") {
        serveHttp(socket, request.method, request.target);
        net::closeSocket(socket);
        return;
    }
    const auto key = request.headers.find("sec-websocket-key");
    if (request.target != "/ws" || key == request.headers.end() || key->second.empty()) {
        httpResponse(socket, 400, "Bad Request", "text/plain", "Invalid WebSocket request");
        net::closeSocket(socket);
        return;
    }
    const std::string response =
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + net::webSocketAccept(key->second) + "\r\n\r\n";
    if (!net::sendAll(socket, response)) {
        net::closeSocket(socket);
        return;
    }

    auto client = std::make_shared<Client>(socket);
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.push_back(client);
    }
    client->playerId = world_.addCharacter("Fresh Mistake", false);
    sendTo(client, "{\"type\":\"welcome\",\"id\":" + std::to_string(client->playerId) +
                       ",\"world\":" + world_.description() + "}");
    while (running_ && client->alive) {
        std::string payload;
        int opcode = 0;
        if (!net::receiveWebSocket(socket, payload, opcode)) break;
        if (opcode == 8) {
            sendTo(client, "", 8);
            break;
        }
        if (opcode == 9) {
            sendTo(client, payload, 10);
        } else if (opcode == 1) {
            handleMessage(client, payload);
        }
    }
    world_.removeCharacter(client->playerId);
    removeClient(client);
}

void GameServer::handleMessage(const std::shared_ptr<Client>& client, const std::string& text) {
    bool valid = false;
    const JsonValue message = JsonValue::parse(text, valid);
    if (!valid) return;
    const std::string type = message.stringAt("type");
    const std::uint32_t id = client->playerId;
    if (type == "input") {
        const float x = static_cast<float>(std::clamp(message.numberAt("x"), -1.0, 1.0));
        const float z = static_cast<float>(std::clamp(message.numberAt("z"), -1.0, 1.0));
        world_.setInput(id, {x, z}, message.boolAt("sprint"), message.boolAt("piss"),
                        static_cast<float>(message.numberAt("yaw")),
                        static_cast<float>(message.numberAt("pitch")), message.boolAt("slow"),
                        message.boolAt("crouch"), message.boolAt("grab"), message.boolAt("jump"),
                        message.boolAt("block"),
                        {static_cast<float>(message.numberAt("aimX")), static_cast<float>(message.numberAt("aimZ"))},
                        static_cast<float>(message.numberAt("aimY")), message.boolAt("aim"));
    } else if (type == "zip") {
        world_.toggleZip(id);
    } else if (type == "path") {
        const float x = static_cast<float>(std::clamp(message.numberAt("x"), -8.7, 8.7));
        const float z = static_cast<float>(std::clamp(message.numberAt("z"), -6.7, 6.7));
        world_.setDestination(id, {x, z}, message.boolAt("sprint"), message.boolAt("advanced"));
    } else if (type == "attack") {
        const double target = std::clamp(message.numberAt("target"), 0.0, 4294967295.0);
        world_.attack(id, static_cast<std::uint32_t>(target), message.boolAt("advanced"));
    } else if (type == "toilet") {
        world_.requestToilet(id, message.stringAt("action", "sit"));
    } else if (type == "customize") {
        Appearance appearance = world_.character(id).appearance;
        appearance.male = message.boolAt("male", appearance.male);
        appearance.skin = static_cast<int>(message.numberAt("skin", appearance.skin));
        appearance.hair = static_cast<int>(message.numberAt("hair", appearance.hair));
        appearance.hairColor = static_cast<int>(message.numberAt("hairColor", appearance.hairColor));
        appearance.shirt = static_cast<int>(message.numberAt("shirt", appearance.shirt));
        appearance.pants = static_cast<int>(message.numberAt("pants", appearance.pants));
        appearance.beard = static_cast<int>(message.numberAt("beard", appearance.beard));
        appearance.coat = message.boolAt("coat", appearance.coat);
        appearance.sunglasses = message.boolAt("sunglasses", appearance.sunglasses);
        appearance.eyeColor = static_cast<int>(message.numberAt("eyeColor", appearance.eyeColor));
        appearance.makeup = static_cast<int>(message.numberAt("makeup", appearance.makeup));
        appearance.lipColor = static_cast<int>(message.numberAt("lipColor", appearance.lipColor));
        appearance.suit = message.boolAt("suit", appearance.suit);
        appearance.tie = static_cast<int>(message.numberAt("tie", appearance.tie));
        appearance.dress = message.boolAt("dress", appearance.dress);
        appearance.beret = message.boolAt("beret", appearance.beret);
        appearance.earrings = message.boolAt("earrings", appearance.earrings);
        appearance.necklace = message.boolAt("necklace", appearance.necklace);
        appearance.fur = message.boolAt("fur", appearance.fur);
        if (const JsonValue* morphs = message.get("morphs")) {
            const auto& values = morphs->array();
            for (std::size_t i = 0; i < appearance.morphs.size() && i < values.size(); ++i) {
                appearance.morphs[i] = static_cast<int>(values[i].number(appearance.morphs[i]));
            }
        }
        world_.customizeCharacter(id, appearance);
    } else if (type == "cheat") {
        world_.adjustNeed(id, message.stringAt("need"), static_cast<float>(message.numberAt("delta")));
    } else if (type == "join") {
        world_.renameCharacter(id, message.stringAt("name", "Fresh Mistake"));
    }
}

void GameServer::serveHttp(SocketHandle socket, const std::string& method, const std::string& target) {
    if (method != "GET" && method != "HEAD") {
        httpResponse(socket, 405, "Method Not Allowed", "text/plain", "Use GET");
        return;
    }
    std::string decoded;
    const std::string withoutQuery = target.substr(0, target.find('?'));
    if (!decodeUrl(withoutQuery, decoded)) {
        httpResponse(socket, 400, "Bad Request", "text/plain", "Bad URL");
        return;
    }
    std::replace(decoded.begin(), decoded.end(), '\\', '/');
    std::string relative = decoded == "/" ? "index.html" : decoded;
    while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
    if (relative.empty() || containsParentTraversal(std::filesystem::path(relative))) {
        httpResponse(socket, 403, "Forbidden", "text/plain", "Nice try, heretic");
        return;
    }
    std::error_code error;
    const std::filesystem::path file = std::filesystem::weakly_canonical(webRoot_ / relative, error);
    if (error) {
        httpResponse(socket, 404, "Not Found", "text/plain", "404: Asset died in childbirth");
        return;
    }
    const std::string rootText = webRoot_.string();
    const std::string fileText = file.string();
    const bool withinRoot = fileText.size() > rootText.size() &&
                            fileText.compare(0, rootText.size(), rootText) == 0 &&
                            (fileText[rootText.size()] == '/' || fileText[rootText.size()] == '\\');
    if (!withinRoot) {
        httpResponse(socket, 403, "Forbidden", "text/plain", "Nice try, heretic");
        return;
    }
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        httpResponse(socket, 404, "Not Found", "text/plain", "404: Asset died in childbirth");
        return;
    }
    const std::string body((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (method == "HEAD") {
        httpResponse(socket, 200, "OK", mimeType(file), "");
    } else {
        httpResponse(socket, 200, "OK", mimeType(file), body);
    }
}

void GameServer::simulationLoop() {
    using Clock = std::chrono::steady_clock;
    auto next = Clock::now();
    int broadcastDivider = 0;
    while (running_) {
        next += std::chrono::milliseconds(50);
        world_.update(0.05F);
        if (++broadcastDivider >= 2) {
            broadcastDivider = 0;
            broadcast(world_.snapshot());
        }
        std::this_thread::sleep_until(next);
        if (Clock::now() - next > std::chrono::milliseconds(250)) next = Clock::now();
    }
}

void GameServer::broadcast(const std::string& message) {
    std::vector<std::shared_ptr<Client>> recipients;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        recipients = clients_;
    }
    for (const auto& client : recipients) {
        if (client->alive) sendTo(client, message);
    }
}

void GameServer::sendTo(const std::shared_ptr<Client>& client, const std::string& message, int opcode) {
    std::lock_guard<std::mutex> lock(client->writer);
    if (!net::sendWebSocket(client->socket, message, opcode)) client->alive = false;
}

void GameServer::removeClient(const std::shared_ptr<Client>& client) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_.erase(std::remove(clients_.begin(), clients_.end(), client), clients_.end());
}

} // namespace gtc
