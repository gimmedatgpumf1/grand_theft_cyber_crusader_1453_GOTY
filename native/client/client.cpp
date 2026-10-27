#include "client/client.h"

#include "core/json.h"
#include "net/sha1.h"
#include "net/websocket.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <random>
#include <sstream>

namespace gtc::native {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) return "";
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}

std::string base64(const unsigned char* data, std::size_t size) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (std::size_t i = 0; i < size; i += 3) {
        const std::uint32_t a = data[i];
        const std::uint32_t b = i + 1 < size ? data[i + 1] : 0;
        const std::uint32_t c = i + 2 < size ? data[i + 2] : 0;
        const std::uint32_t value = (a << 16U) | (b << 8U) | c;
        result.push_back(alphabet[(value >> 18U) & 63U]);
        result.push_back(alphabet[(value >> 12U) & 63U]);
        result.push_back(i + 1 < size ? alphabet[(value >> 6U) & 63U] : '=');
        result.push_back(i + 2 < size ? alphabet[value & 63U] : '=');
    }
    return result;
}

std::string webSocketKey() {
    std::array<unsigned char, 16> bytes{};
    std::random_device random;
    for (unsigned char& byte : bytes) byte = static_cast<unsigned char>(random());
    return base64(bytes.data(), bytes.size());
}

bool readUpgradeResponse(SocketHandle socket, std::string& response) {
    response.clear();
    char byte = 0;
    while (response.find("\r\n\r\n") == std::string::npos) {
        if (response.size() >= 16384U || net::readSome(socket, &byte, 1) != 1) return false;
        response.push_back(byte);
    }
    return true;
}

std::string header(const std::string& response, const std::string& name) {
    std::istringstream lines(response);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        if (lower(trim(line.substr(0, colon))) == name) return trim(line.substr(colon + 1));
    }
    return "";
}

ToiletAction toiletAction(const std::string& name) {
    if (name == "sit") return ToiletAction::Sit;
    if (name == "shit") return ToiletAction::Shit;
    if (name == "piss") return ToiletAction::Piss;
    return ToiletAction::None;
}

Appearance parseAppearance(const JsonValue& value) {
    Appearance appearance;
    appearance.male = value.boolAt("male", appearance.male);
    appearance.skin = static_cast<int>(value.numberAt("skin", appearance.skin));
    appearance.hair = static_cast<int>(value.numberAt("hair", appearance.hair));
    appearance.hairColor = static_cast<int>(value.numberAt("hairColor", appearance.hairColor));
    appearance.shirt = static_cast<int>(value.numberAt("shirt", appearance.shirt));
    appearance.pants = static_cast<int>(value.numberAt("pants", appearance.pants));
    appearance.beard = static_cast<int>(value.numberAt("beard", appearance.beard));
    appearance.coat = value.boolAt("coat", appearance.coat);
    appearance.sunglasses = value.boolAt("sunglasses", appearance.sunglasses);
    appearance.eyeColor = static_cast<int>(value.numberAt("eyeColor", appearance.eyeColor));
    appearance.makeup = static_cast<int>(value.numberAt("makeup", appearance.makeup));
    appearance.lipColor = static_cast<int>(value.numberAt("lipColor", appearance.lipColor));
    appearance.suit = value.boolAt("suit", appearance.suit);
    appearance.tie = static_cast<int>(value.numberAt("tie", appearance.tie));
    appearance.dress = value.boolAt("dress", appearance.dress);
    appearance.beret = value.boolAt("beret", appearance.beret);
    appearance.earrings = value.boolAt("earrings", appearance.earrings);
    appearance.necklace = value.boolAt("necklace", appearance.necklace);
    appearance.fur = value.boolAt("fur", appearance.fur);
    if (const JsonValue* morphs = value.get("morphs")) {
        const auto& values = morphs->array();
        for (std::size_t i = 0; i < appearance.morphs.size() && i < values.size(); ++i) {
            appearance.morphs[i] = static_cast<int>(values[i].number());
        }
    }
    return appearance;
}

Character parseCharacter(const JsonValue& value) {
    Character character;
    character.id = static_cast<std::uint32_t>(value.numberAt("id"));
    character.name = value.stringAt("name");
    character.color = value.stringAt("color", "#d7ff54");
    character.ai = value.boolAt("ai");
    character.position = {static_cast<float>(value.numberAt("x")), static_cast<float>(value.numberAt("z"))};
    character.yaw = static_cast<float>(value.numberAt("yaw"));
    character.pitch = static_cast<float>(value.numberAt("pitch"));
    character.speed = static_cast<float>(value.numberAt("speed"));
    character.needs.bladder = static_cast<float>(value.numberAt("bladder"));
    character.needs.bowel = static_cast<float>(value.numberAt("bowel"));
    character.pissing = value.boolAt("pissing");
    character.onToilet = value.boolAt("onToilet");
    character.toiletAction = toiletAction(value.stringAt("toiletAction"));
    character.appearance = parseAppearance(value);
    character.noticeSequence = static_cast<std::uint32_t>(value.numberAt("notice"));
    character.notice = value.stringAt("noticeText");
    if (const JsonValue* path = value.get("path")) {
        for (const JsonValue& point : path->array()) {
            const auto& coordinates = point.array();
            if (coordinates.size() >= 2) {
                character.path.push_back({static_cast<float>(coordinates[0].number()),
                                          static_cast<float>(coordinates[1].number())});
            }
        }
    }
    return character;
}

void parseWorld(const JsonValue& value, ClientWorld& world) {
    world.width = static_cast<float>(value.numberAt("width", world.width));
    world.depth = static_cast<float>(value.numberAt("depth", world.depth));
    world.interactionRange = static_cast<float>(value.numberAt("interactionRange", world.interactionRange));
    if (const JsonValue* toilet = value.get("toilet")) {
        world.toilet = {static_cast<float>(toilet->numberAt("x")), static_cast<float>(toilet->numberAt("z"))};
        world.toiletFront = {static_cast<float>(toilet->numberAt("frontX")),
                             static_cast<float>(toilet->numberAt("frontZ"))};
    }
    world.obstacles.clear();
    if (const JsonValue* obstacles = value.get("obstacles")) {
        for (const JsonValue& item : obstacles->array()) {
            Obstacle obstacle;
            obstacle.center = {static_cast<float>(item.numberAt("x")), static_cast<float>(item.numberAt("z"))};
            obstacle.halfSize = {static_cast<float>(item.numberAt("hx")), static_cast<float>(item.numberAt("hz"))};
            obstacle.height = static_cast<float>(item.numberAt("height", 3.0));
            obstacle.kind = item.stringAt("kind");
            world.obstacles.push_back(std::move(obstacle));
        }
    }
}

void parseState(const JsonValue& value, ClientState& state) {
    state.tick = static_cast<std::uint64_t>(value.numberAt("tick"));
    state.time = static_cast<std::uint64_t>(value.numberAt("time"));
    state.toiletUser = static_cast<std::uint32_t>(value.numberAt("toiletUser"));
    state.characters.clear();
    if (const JsonValue* characters = value.get("characters")) {
        for (const JsonValue& item : characters->array()) state.characters.push_back(parseCharacter(item));
    }
    state.puddles.clear();
    if (const JsonValue* puddles = value.get("puddles")) {
        for (const JsonValue& item : puddles->array()) {
            Puddle puddle;
            puddle.id = static_cast<std::uint32_t>(item.numberAt("id"));
            puddle.position = {static_cast<float>(item.numberAt("x")), static_cast<float>(item.numberAt("z"))};
            puddle.size = static_cast<float>(item.numberAt("size", 0.25));
            state.puddles.push_back(puddle);
        }
    }
    state.events.clear();
    if (const JsonValue* events = value.get("events")) {
        for (const JsonValue& item : events->array()) {
            state.events.push_back({static_cast<std::uint32_t>(item.numberAt("id")), item.stringAt("text")});
        }
    }
}

} // namespace

const Character* ClientState::character(std::uint32_t id) const {
    for (const Character& character : characters) if (character.id == id) return &character;
    return nullptr;
}

NativeClient::~NativeClient() { disconnect(); }

void NativeClient::disconnect() {
    if (socket_ != kInvalidSocket) {
        net::closeSocket(socket_);
        socket_ = kInvalidSocket;
    }
    if (networkInitialized_) {
        net::cleanup();
        networkInitialized_ = false;
    }
    playerId_ = 0;
}

bool NativeClient::connect(const std::string& host, std::uint16_t port, std::string& error) {
    disconnect();
    if (!net::initialize()) {
        error = "Could not initialize native networking";
        return false;
    }
    networkInitialized_ = true;
    socket_ = net::connectClient(host, port);
    if (socket_ == kInvalidSocket) {
        error = "Could not connect to " + host + ":" + std::to_string(port) + ": " + net::lastError();
        disconnect();
        return false;
    }
    const std::string key = webSocketKey();
    std::ostringstream request;
    request << "GET /ws HTTP/1.1\r\nHost: " << host << ':' << port
            << "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " << key
            << "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    std::string response;
    if (!net::sendAll(socket_, request.str()) || !readUpgradeResponse(socket_, response)) {
        error = "Native WebSocket upgrade failed";
        disconnect();
        return false;
    }
    if (response.rfind("HTTP/1.1 101", 0) != 0 || header(response, "sec-websocket-accept") != net::webSocketAccept(key)) {
        error = "Authoritative server rejected the WebSocket upgrade";
        disconnect();
        return false;
    }
    while (playerId_ == 0 && connected()) {
        if (!receive(error)) {
            disconnect();
            return false;
        }
    }
    return true;
}

bool NativeClient::receive(std::string& error) {
    while (connected()) {
        std::string payload;
        int opcode = 0;
        if (!net::receiveWebSocket(socket_, payload, opcode, false)) {
            error = "Authoritative server connection closed";
            disconnect();
            return false;
        }
        if (opcode == 8) {
            net::sendWebSocketClient(socket_, payload, 8);
            error = "Authoritative server closed the WebSocket";
            disconnect();
            return false;
        }
        if (opcode == 9) {
            if (!net::sendWebSocketClient(socket_, payload, 10)) return false;
            continue;
        }
        if (opcode == 1) return parseMessage(payload, error);
    }
    return false;
}

bool NativeClient::parseMessage(const std::string& text, std::string& error) {
    bool valid = false;
    const JsonValue message = JsonValue::parse(text, valid);
    if (!valid) {
        error = "Invalid JSON from authoritative server";
        return false;
    }
    const std::string type = message.stringAt("type");
    if (type == "welcome") {
        playerId_ = static_cast<std::uint32_t>(message.numberAt("id"));
        if (const JsonValue* world = message.get("world")) parseWorld(*world, world_);
        return playerId_ != 0;
    }
    if (type == "state") {
        parseState(message, state_);
        ++stateVersion_;
        return true;
    }
    return true;
}

bool NativeClient::send(const std::string& json) {
    return connected() && net::sendWebSocketClient(socket_, json);
}

bool NativeClient::sendJoin(const std::string& name) {
    return send("{\"type\":\"join\",\"name\":\"" + jsonEscape(name) + "\"}");
}

bool NativeClient::sendInput(Vec2 move, bool sprint, bool piss, float yaw, float pitch) {
    std::ostringstream out;
    out << "{\"type\":\"input\",\"x\":" << jsonNumber(move.x) << ",\"z\":" << jsonNumber(move.y)
        << ",\"sprint\":" << (sprint ? "true" : "false") << ",\"piss\":" << (piss ? "true" : "false")
        << ",\"yaw\":" << jsonNumber(yaw) << ",\"pitch\":" << jsonNumber(pitch) << '}';
    return send(out.str());
}

bool NativeClient::sendPath(Vec2 destination) {
    return send("{\"type\":\"path\",\"x\":" + jsonNumber(destination.x) +
                ",\"z\":" + jsonNumber(destination.y) + "}");
}

bool NativeClient::sendToilet(const std::string& action) {
    return send("{\"type\":\"toilet\",\"action\":\"" + jsonEscape(action) + "\"}");
}

bool NativeClient::sendCheat(const std::string& need, float delta) {
    return send("{\"type\":\"cheat\",\"need\":\"" + jsonEscape(need) +
                "\",\"delta\":" + jsonNumber(delta) + "}");
}

std::string customizeMessage(const Appearance& appearance) {
    std::ostringstream out;
    out << "{\"type\":\"customize\",\"male\":" << (appearance.male ? "true" : "false")
        << ",\"skin\":" << appearance.skin << ",\"hair\":" << appearance.hair
        << ",\"hairColor\":" << appearance.hairColor << ",\"shirt\":" << appearance.shirt
        << ",\"pants\":" << appearance.pants << ",\"beard\":" << appearance.beard
        << ",\"coat\":" << (appearance.coat ? "true" : "false")
        << ",\"sunglasses\":" << (appearance.sunglasses ? "true" : "false")
        << ",\"eyeColor\":" << appearance.eyeColor << ",\"makeup\":" << appearance.makeup
        << ",\"lipColor\":" << appearance.lipColor << ",\"suit\":" << (appearance.suit ? "true" : "false")
        << ",\"tie\":" << appearance.tie << ",\"dress\":" << (appearance.dress ? "true" : "false")
        << ",\"beret\":" << (appearance.beret ? "true" : "false")
        << ",\"earrings\":" << (appearance.earrings ? "true" : "false")
        << ",\"necklace\":" << (appearance.necklace ? "true" : "false")
        << ",\"fur\":" << (appearance.fur ? "true" : "false") << ",\"morphs\":[";
    for (std::size_t i = 0; i < appearance.morphs.size(); ++i) {
        if (i != 0) out << ',';
        out << appearance.morphs[i];
    }
    out << "]}";
    return out.str();
}

bool NativeClient::sendCustomize(const Appearance& appearance) { return send(customizeMessage(appearance)); }

} // namespace gtc::native
