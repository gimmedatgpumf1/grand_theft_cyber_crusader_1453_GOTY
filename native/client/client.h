#pragma once

#include "core/types.h"
#include "net/platform.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gtc::native {

struct ClientWorld {
    float width = 18.0F;
    float depth = 14.0F;
    float interactionRange = 1.8F;
    Vec2 toilet;
    Vec2 toiletFront;
    std::vector<Obstacle> obstacles;
};

struct ClientState {
    double tick = 0;
    std::uint64_t time = 0;
    double gameTime = 8.0 * 60.0 * 60.0;
    std::uint32_t day = 1;
    Vec2 wind;
    std::uint32_t toiletUser = 0;
    std::vector<Character> characters;
    std::vector<Puddle> puddles;
    std::vector<WaterRipple> waterRipples;
    std::vector<Projectile> projectiles;
    std::vector<GameEvent> events;

    const Character* character(std::uint32_t id) const;
};

// Render between two fixed simulation snapshots; partialTick is in [0, 1].
void interpolateState(const ClientState& previous, const ClientState& target,
                      ClientState& output, double partialTick);

class NativeClient {
public:
    NativeClient() = default;
    ~NativeClient();
    NativeClient(const NativeClient&) = delete;
    NativeClient& operator=(const NativeClient&) = delete;

    bool connect(const std::string& host, std::uint16_t port, std::string& error);
    void disconnect();
    bool receive(std::string& error, int timeoutMilliseconds = -1);

    bool sendJoin(const std::string& name);
    bool sendInput(Vec2 move, bool sprint, bool piss, float yaw, float pitch, bool slowWalk = false,
                   bool crouch = false, bool grab = false, bool jump = false, bool block = false,
                   Vec2 aimTarget = {}, float aimHeight = 0.0F, bool hasAimTarget = false);
    bool sendPath(Vec2 destination, bool sprint = false, bool advanced = false);
    bool sendAttack(std::uint32_t targetId, bool advanced = false);
    bool sendAttackAt(Vec2 target, float height);
    bool sendToggleZip();
    bool sendToilet(const std::string& action);
    bool sendCheat(const std::string& need, float delta);
    bool sendCustomize(const Appearance& appearance);

    bool connected() const { return socket_ != kInvalidSocket; }
    std::uint32_t playerId() const { return playerId_; }
    std::uint64_t stateVersion() const { return stateVersion_; }
    const ClientWorld& world() const { return world_; }
    const ClientState& state() const { return state_; }

private:
    bool send(const std::string& json);
    bool parseMessage(const std::string& text, std::string& error);

    SocketHandle socket_ = kInvalidSocket;
    bool networkInitialized_ = false;
    std::uint32_t playerId_ = 0;
    std::uint64_t stateVersion_ = 0;
    ClientWorld world_;
    ClientState state_;
};

std::string customizeMessage(const Appearance& appearance);

} // namespace gtc::native
