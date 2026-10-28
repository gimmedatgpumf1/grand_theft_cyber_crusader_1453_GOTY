#pragma once

#include "core/types.h"
#include "net/platform.h"

// These two structs are shared by the simulator, the renderer and the test
// harness, so their vectors are named with an explicit namespace. An
// unqualified `Vec2` resolves differently depending on which header is
// included first (`gtc::Vec2` is two floats, `gtc::native::Vec2` two doubles),
// and that silently gave the same struct two different layouts in different
// translation units - a class of bug that ends in a stack smash, not a warning.

#include <cstdint>
#include <string>
#include <vector>

namespace gtc::native {

struct ClientWorld {
    float width = 18.0F;
    float depth = 14.0F;
    float interactionRange = 1.8F;
    gtc::Vec2 toilet;
    gtc::Vec2 toiletFront;
    std::vector<Obstacle> obstacles;
};

struct ClientState {
    double tick = 0;
    std::uint64_t time = 0;
    double gameTime = 8.0 * 60.0 * 60.0;
    std::uint32_t day = 1;
    gtc::Vec2 wind;
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
    bool sendInput(gtc::Vec2 move, bool sprint, bool piss, float yaw, float pitch, bool slowWalk = false,
                   bool crouch = false, bool grab = false, bool jump = false, bool block = false,
                   gtc::Vec2 aimTarget = {}, float aimHeight = 0.0F, bool hasAimTarget = false);
    bool sendPath(gtc::Vec2 destination, bool sprint = false, bool advanced = false);
    bool sendAttack(std::uint32_t targetId, bool advanced = false);
    bool sendAttackAt(gtc::Vec2 target, float height);
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
