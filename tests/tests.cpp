#include "core/collision.h"
#include "core/json.h"
#include "core/pathfinding.h"
#include "core/projectile.h"
#include "core/world.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "  FAIL: " << message << '\n';
        ++failures;
    }
}

void test(const std::string& name, const std::function<void()>& body) {
    std::cout << "[test] " << name << '\n';
    body();
}

void moveToToilet(gtc::World& world, std::uint32_t id) {
    world.setDestination(id, {0, -2.85F});
    for (int i = 0; i < 600 && !world.character(id).path.empty(); ++i) world.update(0.05F);
    check(gtc::distance(world.character(id).position, {0, -2.85F}) < 0.2F,
          "explicit movement should reach the toilet");
}

bool driveTo(gtc::World& world, std::uint32_t id, const gtc::Vec2& target) {
    for (int i = 0; i < 1000; ++i) {
        const gtc::Character current = world.character(id);
        if (gtc::distance(current.position, target) < 0.12F) {
            world.setInput(id, {}, false, false, current.yaw, 0);
            world.update(0.05F);
            return true;
        }
        const gtc::Vec2 direction = gtc::normalized(target - current.position);
        world.setInput(id, direction, false, false, std::atan2(direction.x, direction.y), 0);
        world.update(0.05F);
    }
    return false;
}

void place(gtc::World& world, std::uint32_t id, const gtc::Vec2& target) {
    check(driveTo(world, id, target), "test setup position is reachable");
}

} // namespace

int main() {
    test("JSON input parser", [] {
        bool valid = false;
        const auto value = gtc::JsonValue::parse(
            "{\"type\":\"input\",\"x\":0.5,\"piss\":true,\"name\":\"A \\\"citizen\\\"\"}", valid);
        check(valid, "valid JSON should parse");
        check(value.stringAt("type") == "input", "string property");
        check(std::abs(value.numberAt("x") - 0.5) < 0.001, "number property");
        check(value.boolAt("piss"), "boolean property");
        check(value.stringAt("name") == "A \"citizen\"", "escaped string");
        bool invalid = true;
        gtc::JsonValue::parse("{definitely not json}", invalid);
        check(!invalid, "invalid JSON should be rejected");
    });

    test("collision slides instead of crossing objects", [] {
        const std::vector<gtc::Obstacle> obstacles{{{{0, 0}, {1, 1}, 2, "crate"}}};
        const gtc::Vec2 result = gtc::moveWithCollisions({-2, 0}, {4, 0.4F}, 0.35F,
                                                          obstacles, 5, 5);
        check(result.x < -1.3F, "body must remain on the near side");
        check(result.y > 0.3F, "unblocked axis should still slide");
        check(!gtc::circleHitsWorld(result, 0.35F, obstacles, 5, 5), "resolved body is valid");
    });

    test("A* paths around a solid obstacle", [] {
        const std::vector<gtc::Obstacle> obstacles{{{{0, 0}, {1, 1}, 2, "crate"}}};
        const gtc::Pathfinder pathfinder(obstacles, 5, 5);
        const auto path = pathfinder.findPath({-3, 0}, {3, 0});
        check(!path.empty(), "a route exists");
        gtc::Vec2 previous{-3, 0};
        for (const auto& point : path) {
            check(gtc::lineIsWalkable(previous, point, 0.38F, obstacles, 5, 5),
                  "every simplified segment remains collision-free");
            previous = point;
        }
        check(gtc::distance(previous, {3, 0}) < 0.01F, "route reaches requested destination");
    });

    test("toilet range, persistent sitting, and standing", [] {
        gtc::World world;
        const auto id = world.addCharacter("Test Subject", false);
        const auto spawn = world.character(id).position;
        world.requestToilet(id, "sit");
        check(world.character(id).toiletAction == gtc::ToiletAction::None,
              "out-of-range interaction must not start an action");
        check(world.character(id).path.empty(), "out-of-range interaction must not auto-walk");
        check(gtc::distance(world.character(id).position, spawn) < 0.001F,
              "out-of-range interaction must not move the citizen");
        moveToToilet(world, id);
        world.requestToilet(id, "sit");
        check(world.character(id).onToilet, "in-range sit should seat immediately");
        world.setInput(id, {}, false, false, 1.25F, 0.4F);
        for (int i = 0; i < 140; ++i) world.update(0.05F);
        check(world.character(id).onToilet, "sitting persists beyond the old six-second action");
        check(std::abs(world.character(id).yaw) < 0.001F,
              "camera input must not rotate a seated citizen");
        world.requestToilet(id, "stand");
        check(world.character(id).toiletAction == gtc::ToiletAction::None,
              "explicit stand releases the throne");
        world.requestToilet(id, "sit");
        world.setInput(id, {0, 1}, false, false, 0.0F, 0.0F);
        world.update(0.05F);
        check(world.character(id).toiletAction == gtc::ToiletAction::None,
              "WASD interrupts sitting and returns movement control");
    });

    test("manual backward movement preserves body facing", [] {
        gtc::World world;
        const auto id = world.addCharacter("Backward Subject", false);
        const gtc::Vec2 spawn = world.character(id).position;
        constexpr float facing = 3.14159265F;
        world.setInput(id, {0, 1}, false, false, facing, 0);
        world.update(0.05F);
        const gtc::Character backward = world.character(id);
        check(backward.position.y > spawn.y, "backward input moves opposite the facing direction");
        check(std::abs(backward.yaw - facing) < 0.0001F,
              "manual backward movement must not rotate the body toward velocity");
        check(backward.manualMove.y > 0.99F, "authoritative movement remains backward");
        check(world.snapshot().find("\"moveZ\":1") != std::string::npos,
              "movement direction is replicated for backward gait rendering");
    });

    test("player and NPC locomotion use measured speeds", [] {
        gtc::World world;
        const auto id = world.addCharacter("Speed Subject", false);
        gtc::Vec2 previous = world.character(id).position;
        world.setInput(id, {0, 1}, false, false, 0, 0);
        world.update(0.05F);
        gtc::Character current = world.character(id);
        check(std::abs(current.speed * 3.6F - 6.4F) < 0.001F,
              "player defaults to 6.4 kilometres per hour");
        check(std::abs(gtc::distance(current.position, previous) - current.speed * 0.05F) < 0.0001F,
              "fast walking displacement matches metres per second");
        previous = current.position;
        world.setInput(id, {0, 1}, false, false, 0, 0, true);
        world.update(0.05F);
        current = world.character(id);
        check(std::abs(current.speed * 3.6F - 4.0F) < 0.001F,
              "Caps Lock input selects four kilometres per hour");
        check(std::abs(gtc::distance(current.position, previous) - current.speed * 0.05F) < 0.0001F,
              "slow walking displacement matches metres per second");
        world.setInput(id, {0, 1}, true, false, 0, 0, true);
        world.update(0.05F);
        check(std::abs(world.character(id).speed * 3.6F - 20.0F) < 0.001F,
              "Shift overrides slow walking with twenty kilometres per hour");

        gtc::World npcWorld;
        const auto npc = npcWorld.addCharacter("Measured NPC", true);
        npcWorld.setDestination(npc, {0, 2.3F});
        npcWorld.update(0.05F);
        check(std::abs(npcWorld.character(npc).speed * 3.6F - 4.0F) < 0.001F,
              "ordinary NPC travel uses slow walking");
        npcWorld.adjustNeed(npc, "bladder", 100);
        for (int i = 0; i < 60; ++i) npcWorld.update(0.05F);
        const gtc::Character urgent = npcWorld.character(npc);
        check(urgent.pendingToilet, "NPC has an urgent toilet destination");
        check(std::abs(urgent.speed * 3.6F - 6.4F) < 0.001F && !urgent.sprint,
              "urgent NPC travel uses fast walking without sprinting");
    });

    test("crouch and prone stances use exact speeds and replicate", [] {
        gtc::World world;
        const auto id = world.addCharacter("Low Profile", false);
        gtc::Vec2 previous = world.character(id).position;
        world.setInput(id, {0, 1}, false, false, 0, 0, false, true);
        world.update(0.05F);
        gtc::Character current = world.character(id);
        check(current.crouched && !current.prone, "Alt selects crouching without prone");
        check(std::abs(current.speed * 3.6F - 3.6F) < 0.001F, "crouch speed is 3.6 kilometres per hour");
        check(std::abs(gtc::distance(current.position, previous) - 0.05F) < 0.0001F,
              "crouch displacement is one metre per second");
        previous = current.position;
        world.setInput(id, {0, 1}, false, false, 0, 0, false, true, true);
        world.update(0.05F);
        current = world.character(id);
        check(current.prone && !current.crouched, "crouch plus grab lays the citizen down");
        check(std::abs(current.speed * 3.6F - 2.0F) < 0.001F, "prone speed is two kilometres per hour");
        check(std::abs(gtc::distance(current.position, previous) - gtc::kProneSpeed * 0.05F) < 0.0001F,
              "prone displacement matches two kilometres per hour");
        check(current.charge == 0.0F, "prone grab does not charge a jump");
        check(world.snapshot().find("\"prone\":true") != std::string::npos,
              "stance is replicated to first- and third-person renderers");
        const gtc::Obstacle overhang{{0, 0}, {1, 1}, 2.0, "tunnel", 0.75F};
        check(gtc::bodyHitsObstacle({0, 0}, 0.3F, 0, gtc::kStandingHeight, overhang),
              "standing body cannot fit below a 75 centimetre overhang");
        check(!gtc::bodyHitsObstacle({0, 0}, 0.3F, 0, gtc::kProneHeight, overhang),
              "prone body fits below the same overhang");
    });

    test("jump apex, charged jump, and charge expiry", [] {
        gtc::World world;
        const auto id = world.addCharacter("Ballistic Subject", false);
        const auto airborneApex = [&](float expected) {
            world.update(0.05F);
            world.setInput(id, {}, false, false, 0, 0);
            float apex = world.character(id).elevation;
            for (int i = 0; i < 60; ++i) {
                world.update(0.05F);
                apex = std::max(apex, world.character(id).elevation);
                if (i > 5 && world.character(id).grounded) break;
            }
            check(std::abs(apex - expected) < 0.035F,
                  "jump apex matches authored height " + std::to_string(expected));
            check(world.character(id).grounded && std::abs(world.character(id).elevation) < 0.001F,
                  "jump lands back on the floor");
        };
        world.setInput(id, {}, false, false, 0, 0, false, false, false, true);
        airborneApex(gtc::kJumpHeight);
        for (int i = 0; i < 30; ++i) {
            world.setInput(id, {}, false, false, 0, 0, false, false, true);
            world.update(0.05F);
        }
        check(std::abs(world.character(id).charge - 0.5F) < 0.02F, "half charge takes 1.5 seconds");
        world.setInput(id, {}, false, false, 0, 0, false, false, true, true);
        airborneApex(0.75F);
        for (int i = 0; i < 60; ++i) {
            world.setInput(id, {}, false, false, 0, 0, false, false, true);
            world.update(0.05F);
        }
        check(world.character(id).charge > 0.99F, "full charge takes three seconds");
        for (int i = 0; i < 40; ++i) {
            world.setInput(id, {}, false, false, 0, 0, false, false, true);
            world.update(0.05F);
        }
        check(world.character(id).charge == 0.0F, "full charge dissipates after two seconds");
        world.setInput(id, {}, false, false, 0, 0);
        world.update(0.05F);
        for (int i = 0; i < 60; ++i) {
            world.setInput(id, {}, false, false, 0, 0, false, false, true);
            world.update(0.05F);
        }
        world.setInput(id, {}, false, false, 0, 0, false, false, true, true);
        airborneApex(gtc::kMaximumJumpHeight);
    });

    test("automatic and slow steps support the citizen", [] {
        gtc::World world;
        const auto id = world.addCharacter("Stair Subject", false);
        check(driveTo(world, id, {3.1F, -0.2F}), "approach the low step");
        bool lowStep = false;
        float lowSpeed = 0;
        for (int i = 0; i < 30 && !lowStep; ++i) {
            world.setInput(id, {0, -1}, false, false, 3.14159265F, 0);
            world.update(0.05F);
            lowStep = world.character(id).elevation > 0.17F;
            lowSpeed = world.character(id).speed;
        }
        check(lowStep && std::abs(world.character(id).elevation - 0.18F) < 0.001F,
              "18 centimetre stair steps up automatically");
        check(std::abs(lowSpeed * 3.6F - 6.4F) < 0.001F, "low step has no speed penalty");
        check(driveTo(world, id, {4.8F, -0.2F}) && driveTo(world, id, {4.8F, -2.45F}),
              "approach the higher step from ground level");
        bool highStep = false;
        float highSpeed = 0;
        for (int i = 0; i < 40 && !highStep; ++i) {
            world.setInput(id, {-1, 0}, false, false, -1.57079633F, 0);
            world.update(0.05F);
            highStep = world.character(id).elevation > 0.44F;
            highSpeed = world.character(id).speed;
        }
        check(highStep && std::abs(world.character(id).elevation - 0.45F) < 0.001F,
              "45 centimetre obstacle remains steppable");
        check(highSpeed <= 0.7501F && highSpeed > 0.0F, "higher step is deliberately slow");
        world.setInput(id, {}, false, false, 0, 0);
        for (int i = 0; i < 10; ++i) world.update(0.05F);
        check(world.character(id).grounded && std::abs(world.character(id).elevation - 0.45F) < 0.001F,
              "step top is a persistent support surface");

        gtc::World boundaryWorld;
        const auto boundary = boundaryWorld.addCharacter("Boundary Step", false);
        check(driveTo(boundaryWorld, boundary, {3.1F, -0.2F}), "approach the exact-rise steps");
        for (int i = 0; i < 30 && boundaryWorld.character(boundary).elevation < 0.17F; ++i) {
            boundaryWorld.setInput(boundary, {0, -1}, false, false, 3.14159265F, 0);
            boundaryWorld.update(0.05F);
        }
        bool exactRiseSlow = false;
        for (int i = 0; i < 50; ++i) {
            boundaryWorld.setInput(boundary, {0, -1}, false, false, 3.14159265F, 0);
            boundaryWorld.update(0.05F);
            const auto& c = boundaryWorld.character(boundary);
            exactRiseSlow = exactRiseSlow || (c.grounded && std::abs(c.elevation - 0.18F) < 0.001F &&
                c.position.y < -1.0F && c.speed <= 0.7501F && c.speed > 0.0F);
        }
        check(exactRiseSlow, "an exact 27 centimetre rise uses the slow-step speed");
    });

    test("toilet grab, climb, release, and mid-air grab", [] {
        gtc::World world;
        const auto id = world.addCharacter("Porcelain Climber", false);
        moveToToilet(world, id);
        check(driveTo(world, id, {0, -3.65F}), "move within arm reach of the toilet");
        world.setInput(id, {}, false, false, 3.14159265F, 0, false, false, true);
        world.update(0.05F);
        check(world.character(id).grabbing && std::abs(world.character(id).grabHeight - gtc::kToiletSeatHeight) < 0.001F,
              "Ctrl grabs a reachable toilet ledge and replicates its height");
        world.setInput(id, {0, 1}, false, false, 3.14159265F, 0, false, false, true);
        world.update(0.05F);
        check(!world.character(id).grabbing, "moving backward releases the ledge");
        world.setInput(id, {}, false, false, 3.14159265F, 0, false, false, true);
        world.update(0.05F);
        check(!world.character(id).grabbing, "holding Ctrl cannot instantly regrab after release");
        world.setInput(id, {}, false, false, 3.14159265F, 0);
        world.update(0.05F);
        world.setInput(id, {}, false, false, 3.14159265F, 0, false, false, true);
        world.update(0.05F);
        bool climbed = false;
        for (int i = 0; i < 60 && !climbed; ++i) {
            world.setInput(id, {0, -1}, false, false, 3.14159265F, 0, false, false, true);
            world.update(0.05F);
            climbed = !world.character(id).grabbing && world.character(id).grounded &&
                world.character(id).elevation > gtc::kToiletSeatHeight - 0.01F;
        }
        check(climbed, "moving forward climbs onto the toilet");
        world.setInput(id, {}, false, false, 3.14159265F, 0);
        world.update(0.05F);
        check(world.character(id).grounded && std::abs(world.character(id).elevation - gtc::kToiletSeatHeight) < 0.001F,
              "toilet top remains a support surface after Ctrl is released");

        gtc::World airWorld;
        const auto air = airWorld.addCharacter("Midair Climber", false);
        check(driveTo(airWorld, air, {-2.0F, -5.55F}) && driveTo(airWorld, air, {0, -5.55F}),
              "approach the toilet tank for a mid-air grab");
        airWorld.setInput(air, {}, false, false, 0.0F, 0, false, false, false, true);
        airWorld.update(0.05F);
        airWorld.setInput(air, {}, false, false, 0.0F, 0);
        airWorld.update(0.05F);
        airWorld.setInput(air, {}, false, false, 0.0F, 0, false, false, true);
        airWorld.update(0.05F);
        check(airWorld.character(air).grabbing && airWorld.character(air).verticalVelocity == 0.0F,
              "Ctrl can catch a reachable ledge in mid-air");
    });

    test("ordinary crate uses the same grab and climb mechanics", [] {
        gtc::World world;
        const auto id = world.addCharacter("Crate Climber", false);
        check(driveTo(world, id, {6.7F, 4.75F}), "approach the ordinary crate");
        world.setInput(id, {}, false, false, 3.14159265F, 0, false, false, true);
        world.update(0.05F);
        check(world.character(id).grabbing && std::abs(world.character(id).grabHeight - 0.85F) < 0.001F,
              "Ctrl grabs an ordinary wooden crate and replicates its height");
        bool climbed = false;
        for (int i = 0; i < 40 && !climbed; ++i) {
            world.setInput(id, {0, -1}, false, false, 3.14159265F, 0, false, false, true);
            world.update(0.05F);
            climbed = !world.character(id).grabbing && world.character(id).grounded &&
                std::abs(world.character(id).elevation - 0.85F) < 0.001F;
        }
        check(climbed, "moving forward climbs onto the crate and remains supported");
    });

    test("low tunnel preserves manually selected prone crawling", [] {
        gtc::World world;
        const auto id = world.addCharacter("Tunnel Subject", false);
        check(driveTo(world, id, {-5.0F, 1.4F}), "approach the low tunnel");
        bool unsolicitedStance = false;
        for (int i = 0; i < 45; ++i) {
            world.setInput(id, {0, -1}, false, false, 3.14159265F, 0);
            world.update(0.05F);
            unsolicitedStance = unsolicitedStance || world.character(id).prone || world.character(id).crouched;
        }
        check(!unsolicitedStance, "walking into low clearance never initiates crouch or prone");
        check(world.character(id).position.y > 1.0F, "standing profile is stopped by the tunnel");

        for (int i = 0; i < 90 && world.character(id).position.y > 0.25F; ++i) {
            world.setInput(id, {0, -1}, false, false, 3.14159265F, 0, false, true, true);
            world.update(0.05F);
        }
        check(world.character(id).prone && world.character(id).position.y < 0.30F,
              "manual prone enters the low tunnel");
        bool correctSpeed = true;
        world.setInput(id, {0, -1}, false, false, 3.14159265F, 0);
        world.update(0.05F);
        check(world.character(id).prone, "low clearance preserves prone after stance keys are released");
        for (int i = 0; i < 180 && world.character(id).position.y > -1.35F; ++i) {
            world.setInput(id, {0, -1}, false, false, 3.14159265F, 0);
            world.update(0.05F);
            if (world.character(id).prone && std::abs(world.character(id).speed * 3.6F - 2.0F) > 0.001F) {
                correctSpeed = false;
            }
        }
        check(correctSpeed, "continued tunnel crawling uses two kilometres per hour");
        check(world.character(id).position.y < -1.2F, "prone profile physically passes through the tunnel");
        check(!world.character(id).prone && !world.character(id).crouched,
              "citizen stands automatically after leaving the tunnel");
    });

    test("shitting drains bowel without magically emptying bladder", [] {
        gtc::World world;
        const auto id = world.addCharacter("Bowel Subject", false);
        moveToToilet(world, id);
        world.adjustNeed(id, "bowel", 100);
        const float bladder = world.character(id).needs.bladder;
        world.requestToilet(id, "shit");
        check(world.character(id).onToilet, "shitting is seated");
        for (int i = 0; i < 700 && world.character(id).toiletAction != gtc::ToiletAction::None; ++i) {
            world.update(0.05F);
        }
        const auto relieved = world.character(id);
        check(relieved.needs.bowel == 0.0F, "shitting empties bowel");
        check(relieved.needs.bladder == bladder, "shitting leaves bladder unchanged");
        check(relieved.toiletAction == gtc::ToiletAction::None, "completed shit releases the throne");
    });

    test("male toilet piss is standing and makes no puddle", [] {
        gtc::World world;
        const auto id = world.addCharacter("Standing Subject", false);
        moveToToilet(world, id);
        world.adjustNeed(id, "bladder", 100);
        const float bowel = world.character(id).needs.bowel;
        world.requestToilet(id, "piss");
        check(!world.character(id).onToilet, "male toilet piss is standing");
        check(world.character(id).pissing, "standing toilet piss has a visible stream state");
        for (int i = 0; i < 700 && world.character(id).toiletAction != gtc::ToiletAction::None; ++i) {
            world.update(0.05F);
        }
        const auto relieved = world.character(id);
        check(relieved.needs.bladder == 0.0F, "toilet piss empties bladder");
        check(relieved.needs.bowel == bowel, "pissing leaves bowel unchanged");
        check(world.snapshot().find("\"puddles\":[{\"id\":") == std::string::npos,
              "pissing into the bowl must not create a floor puddle");
    });

    test("creator validates appearance and female piss is seated", [] {
        gtc::World world;
        const auto id = world.addCharacter("Creator Subject", false);
        gtc::Appearance appearance;
        appearance.male = false;
        appearance.skin = 99;
        appearance.hair = 99;
        appearance.hairColor = 99;
        appearance.morphs.fill(500);
        appearance.morphs[0] = -20;
        appearance.morphs[3] = -500;
        appearance.shirt = 99;
        appearance.pants = -2;
        appearance.beard = 99;
        appearance.coat = true;
        appearance.sunglasses = true;
        world.customizeCharacter(id, appearance);
        const auto customized = world.character(id);
        check(!customized.appearance.male && customized.appearance.skin == 5 &&
              customized.appearance.hair == 15 && customized.appearance.shirt == 6 &&
              customized.appearance.pants == 0 && customized.appearance.beard == 2 &&
              customized.appearance.hairColor == 8,
              "creator fields are validated and clamped");
        check(customized.appearance.morphs[0] == 0 && customized.appearance.morphs[1] == 100 &&
              customized.appearance.morphs[3] == -100 && customized.appearance.morphs[50] == 100,
              "all 51 morph values are server-clamped");
        check(world.snapshot().find("\"morphs\":[0,100,100,-100") != std::string::npos,
              "the morph vector is replicated in snapshots");
        check(world.snapshot().find("\"coat\":true") != std::string::npos,
              "appearance is replicated in snapshots");
        moveToToilet(world, id);
        world.requestToilet(id, "piss");
        check(world.character(id).onToilet && !world.character(id).pissing,
              "female toilet piss uses the seated pose without a standing stream");
        gtc::Appearance male = customized.appearance;
        male.male = true;
        world.customizeCharacter(id, male);
        check(world.character(id).toiletAction == gtc::ToiletAction::None && !world.character(id).onToilet,
              "changing sex interrupts the old seated piss pose");
        world.requestToilet(id, "piss");
        check(!world.character(id).onToilet && world.character(id).pissing,
              "male piss starts standing after customization");
        world.customizeCharacter(id, customized.appearance);
        check(world.character(id).toiletAction == gtc::ToiletAction::None,
              "changing sex also interrupts standing piss");
    });

    test("cheat menu adjusts needs within safe bounds", [] {
        gtc::World world;
        const auto id = world.addCharacter("Cheat Subject", false);
        world.adjustNeed(id, "bladder", 100);
        world.adjustNeed(id, "bowel", 25);
        check(world.character(id).needs.bladder == 100.0F, "bladder can be filled and clamps at 100");
        const float bowel = world.character(id).needs.bowel;
        world.adjustNeed(id, "bladder", -150);
        world.adjustNeed(id, "invalid", 100);
        check(world.character(id).needs.bladder == 0.0F, "bladder can be emptied and clamps at zero");
        check(world.character(id).needs.bowel == bowel, "invalid need names are ignored");
    });

    test("unzipped first-person piss drains bladder and makes puddles", [] {
        gtc::World world;
        const auto id = world.addCharacter("Postal Worker", false);
        check(!world.character(id).unzipped, "male citizens start zipped");
        world.setInput(id, {}, false, true, 0.0F, -0.2F);
        world.update(0.05F);
        check(!world.character(id).pissing, "zipped trousers block manual pissing");
        world.setInput(id, {}, false, false, 0.0F, -0.2F);
        world.toggleZip(id);
        check(world.character(id).unzipped && world.snapshot().find("\"unzipped\":true") != std::string::npos,
              "trouser toggle is authoritative and replicated");
        const float before = world.character(id).needs.bladder;
        world.setInput(id, {}, false, true, 0.0F, -0.2F);
        for (int i = 0; i < 20; ++i) world.update(0.05F);
        const auto after = world.character(id);
        check(after.pissing, "pissing state is active");
        check(after.needs.bladder < before - 3.4F && after.needs.bladder > before - 3.8F, "bladder drains");
        check(world.snapshot().find("\"puddles\":[{\"id\":") != std::string::npos,
              "urine creates persistent world puddles");
        world.setInput(id, {}, false, false, 0.0F, 0.0F);
        world.update(0.05F);
        check(!world.character(id).pissing, "releasing input stops pissing");
        world.toggleZip(id);
        check(!world.character(id).unzipped, "T restores the zipped default");
    });

    test("physical piss rises above head and splashes real surfaces", [] {
        using gtc::projectile_detail::Point;
        const gtc::Obstacle wall{{1, 0}, {0.1F, 2}, 2.0F, "boundary"};
        const auto wallHit = gtc::projectile_detail::sweepSurfaces({0, 1, 0}, {2, 1, 0}, 0.022F, {wall});
        check(wallHit.hit && wallHit.puddle.vertical && wallHit.puddle.normal.x < -0.9F &&
              wallHit.puddle.height > 0.9F, "swept piss leaves a vertical wall stain");
        const gtc::Obstacle crate{{1.4F, 0}, {0.6F, 0.6F}, 0.6F, "crate"};
        const auto topHit = gtc::projectile_detail::sweepSurfaces({1.4F, 1, 0}, {1.4F, 0.4F, 0}, 0.022F, {crate});
        check(topHit.hit && !topHit.puddle.vertical && std::abs(topHit.puddle.height - 0.608F) < 0.001F,
              "swept piss can stain the top of an object");
        const gtc::Obstacle toilet{{0, -4.75F}, {0.2285F, 0.3525F}, gtc::kToiletTankHeight, "toilet"};
        const auto bowlHit = gtc::projectile_detail::sweepSurfaces({0, 0.79F, -4.69F}, {0, 0.20F, -4.72F},
            0.022F, {toilet});
        check(bowlHit.hit && bowlHit.toilet && bowlHit.absorb,
              "the open toilet bowl absorbs descending piss below its real rim");
        std::vector<gtc::Puddle> spots;
        gtc::Puddle input;
        const auto first = gtc::placePuddle(spots, input, 1, {}, 9, 7);
        spots.push_back(first);
        const auto second = gtc::placePuddle(spots, input, 2, {}, 9, 7);
        check(first.id == 1 && second.id == 0 && spots.size() == 1 &&
              spots.front().size > first.size && gtc::length(spots.front().position) < 0.001F,
              "overlapping floor impacts merge into one growing puddle without displacement");

        gtc::World upward;
        const auto shooter = upward.addCharacter("Postal Arc", false);
        place(upward, shooter, {0, 0});
        upward.toggleZip(shooter);
        upward.setInput(shooter, {}, false, true, 0, 1.57079633F);
        float apex = 0, drift = 0;
        for (int i = 0; i < 10; ++i) {
            upward.update(0.05F);
            for (const auto& projectile : upward.projectiles()) {
                apex = std::max(apex, projectile.height);
                drift = std::max(drift, std::abs(projectile.position.x));
            }
        }
        check(apex > gtc::kStandingHeight && apex > 2.4F, "ninety-degree piss physically rises above the head");
        check(drift > 0.005F, "wind physically deflects an initially vertical stream");
        check(upward.snapshot().find("\"projectiles\":[{\"id\":") != std::string::npos,
              "actual projectile positions and velocities replicate");

        gtc::World world;
        const auto id = world.addCharacter("Wall Painter", false);
        place(world, id, {-7.5F, 2});
        world.toggleZip(id);
        world.adjustNeed(id, "bladder", 100);
        world.setInput(id, {}, false, true, -1.57079632679F, 0);
        for (int i = 0; i < 12; ++i) world.update(0.05F);
        check(world.snapshot().find("\"vertical\":true") != std::string::npos,
              "wall splash height and orientation replicate after physical impact");
        const auto wallSpots = world.puddles();
        check(std::count_if(wallSpots.begin(), wallSpots.end(), [](const gtc::Puddle& p) {
            return p.vertical; }) >= 4, "multiple physical wall impacts stain reliably rather than only sampled droplets");

        gtc::World aimed;
        const auto aimer = aimed.addCharacter("Porcelain Marksman", false);
        place(aimed, aimer, {0, -3.55F});
        aimed.toggleZip(aimer);
        aimed.adjustNeed(aimer, "bladder", 100);
        aimed.setInput(aimer, {}, false, true, 3.14159265F, -0.9F, false, false, false, false, false,
                       {0, -4.69F}, gtc::kToiletWaterHeight, true);
        bool emitted = false;
        for (int i = 0; i < 40; ++i) {
            aimed.update(0.05F);
            emitted = emitted || !aimed.projectiles().empty();
        }
        check(emitted && aimed.puddles().empty(),
              "crosshair-converged physical droplets enter the toilet water instead of falling short");
        check(!aimed.waterRipples().empty() && aimed.snapshot().find("\"ripples\":[{\"id\":") != std::string::npos,
              "urine impacts replicate animated toilet-water ripple impulses");

        gtc::World elevated;
        const auto painter = elevated.addCharacter("High Wall Marksman", false);
        place(elevated, painter, {-7.5F, 2});
        elevated.toggleZip(painter);
        elevated.adjustNeed(painter, "bladder", 100);
        elevated.setInput(painter, {}, false, true, -1.57079633F, 0.5F, false, false, false, false, false,
                          {-8.84F, 2}, 1.5F, true);
        for (int i = 0; i < 40; ++i) elevated.update(0.05F);
        bool accurate = false;
        for (const auto& puddle : elevated.puddles()) {
            accurate = accurate || (puddle.vertical && std::abs(puddle.height - 1.5F) < 0.08F);
        }
        check(accurate, "close-range aimed piss compensates gravity and strikes an elevated wall target");
    });

    test("unified floor puddles and reactive hollow toilet water", [] {
        gtc::World floor;
        const auto id = floor.addCharacter("One Puddle", false);
        place(floor, id, {0, 0});
        floor.toggleZip(id);
        floor.adjustNeed(id, "bladder", 100);
        floor.setInput(id, {}, false, true, 0, 0, false, false, false, false, false,
                       {0, 1.0F}, 0.018F, true);
        for (int i = 0; i < 60; ++i) floor.update(0.05F);
        const auto spots = floor.puddles();
        check(spots.size() == 1 && spots.front().size > 0.15F,
              "a sustained stream grows one unified floor puddle at physical impacts");

        gtc::World bullet;
        const auto shooter = bullet.addCharacter("Water Shooter", false);
        place(bullet, shooter, {0, -3.55F});
        bullet.toggleZip(shooter);
        bullet.attackAt(shooter, {0, -4.69F}, gtc::kToiletWaterHeight);
        for (int i = 0; i < 12; ++i) bullet.update(0.05F);
        check(!bullet.waterRipples().empty(), "an aimed physical bullet splashes the real water ellipse");

        gtc::World legs;
        const auto wader = legs.addCharacter("Bowl Wader", false);
        place(legs, wader, {0, -4.69F});
        for (int i = 0; i < 20 && gtc::distance(legs.character(wader).position, {0, -4.69F}) > 0.025F; ++i) {
            const gtc::Vec2 direction = gtc::normalized(gtc::Vec2{0, -4.69F} - legs.character(wader).position);
            legs.setInput(wader, direction, false, false, std::atan2(direction.x, direction.y), 0);
            legs.update(0.05F);
        }
        legs.setInput(wader, {}, false, false, 0, 0);
        for (int i = 0; i < 20; ++i) legs.update(0.05F);
        const auto settled = legs.character(wader);
        check(settled.elevation < 0.08F && settled.waterContact,
              "feet enter the hollow bowl instead of standing on an invisible seat plane");
        check(!legs.waterRipples().empty(), "legs entering and moving in water create ripple feedback");
    });

    test("fifteen-times clock, bladder calibration, and day-long drying", [] {
        gtc::World bladder;
        const auto id = bladder.addCharacter("Twenty One Seconds", false);
        bladder.toggleZip(id);
        bladder.adjustNeed(id, "bladder", 100);
        bladder.adjustNeed(id, "bladder", -25);
        bladder.setInput(id, {}, false, true, 0, 0);
        for (int i = 0; i < 419; ++i) bladder.update(0.05F);
        check(bladder.character(id).needs.bladder > 0.0F, "seventy-five percent still flows before twenty-one seconds");
        bladder.update(0.05F);
        check(bladder.character(id).needs.bladder == 0.0F, "seventy-five percent empties at twenty-one seconds");
        bladder.update(0.05F);
        check(!bladder.character(id).pissing, "the empty bladder stops emission");

        gtc::World wet;
        const auto wetId = wet.addCharacter("Persistent Spot", false);
        place(wet, wetId, {0, 0});
        wet.toggleZip(wetId);
        wet.setInput(wetId, {}, false, true, 0, -0.4F);
        for (int i = 0; i < 30; ++i) wet.update(0.05F);
        wet.setInput(wetId, {}, false, false, 0, 0);
        wet.update(0.05F);
        wet.removeCharacter(wetId);
        check(!wet.puddles().empty(), "physical impacts leave persistent stains");
        const double gameBefore = wet.gameSeconds();
        for (int i = 0; i < 100; ++i) wet.update(0.1F);
        check(std::abs((wet.gameSeconds() - gameBefore) - 150.0) < 0.001,
              "ten real seconds advance one hundred fifty game seconds");
        double youngest = 100000.0;
        for (const auto& puddle : wet.puddles()) youngest = std::min(youngest, puddle.age);
        int steps = 0;
        while (!wet.puddles().empty() && steps < 57620) { wet.update(0.1F); ++steps; }
        check(wet.puddles().empty() && std::abs(steps * 0.1F - (gtc::kPuddleLifetimeSeconds - youngest)) < 0.11F,
              "a stain dries after exactly one twenty-four-hour game day");
    });

    test("god attacks are authoritative, ranged, and blockable", [] {
        gtc::World melee;
        const auto attacker = melee.addCharacter("Bonker", false);
        const auto target = melee.addCharacter("Target", false);
        place(melee, attacker, {0, 0});
        place(melee, target, {0, 1});
        melee.attack(attacker, target);
        check(melee.character(target).health == 88.0F && melee.character(attacker).attackAnim > 0.0F,
              "in-range melee applies damage and attack animation");
        melee.setInput(target, {}, false, false, 0, 0, false, false, false, false, true);
        for (int i = 0; i < 12; ++i) melee.update(0.05F);
        melee.attack(attacker, target);
        check(melee.character(target).blocking && melee.character(target).health > 83.7F &&
              melee.character(target).health < 84.0F, "melee RMB blocking reduces server damage");
        check(melee.snapshot().find("\"health\":") != std::string::npos &&
              melee.snapshot().find("\"blocking\":true") != std::string::npos,
              "combat health and block state replicate");

        gtc::World ranged;
        const auto pisser = ranged.addCharacter("Golden Gun", false);
        const auto victim = ranged.addCharacter("Dry Target", false);
        place(ranged, pisser, {0, 0});
        place(ranged, victim, {0, 2.5F});
        ranged.toggleZip(pisser);
        const float bladder = ranged.character(pisser).needs.bladder;
        ranged.attack(pisser, victim, true);
        const auto aimed = ranged.character(pisser);
        const auto aimedTarget = ranged.character(victim);
        const float expectedYaw = std::atan2(aimedTarget.position.x - aimed.position.x,
                                             aimedTarget.position.y - aimed.position.y);
        check(aimedTarget.health == 100.0F && aimed.needs.bladder < bladder && ranged.projectiles().size() == 1 &&
              std::abs(aimed.yaw - expectedYaw) < 0.0001F,
              "ranged click spawns a physical round instead of applying instant damage");
        check(ranged.character(pisser).path.empty() && ranged.character(pisser).attackAnim > 0.0F,
              "ranged attack never creates an approach path");
        for (int i = 0; i < 20 && !ranged.projectiles().empty(); ++i) ranged.update(0.05F);
        check(ranged.character(victim).health == 93.0F, "the physical round damages only after swept impact");
        place(ranged, victim, {0, 4.0F});
        for (int i = 0; i < 12; ++i) ranged.update(0.05F);
        const float health = ranged.character(victim).health;
        const float attemptBladder = ranged.character(pisser).needs.bladder;
        ranged.attack(pisser, victim, true);
        const auto attempt = ranged.character(pisser);
        check(ranged.character(victim).health == health && attempt.path.empty() && attempt.attackAnim > 0.0F &&
              attempt.needs.bladder < attemptBladder && !ranged.projectiles().empty() &&
              attempt.notice.find("Physical shot") != std::string::npos,
              "long ranged attacks always spawn a round without chasing or refusing");
        for (int i = 0; i < 20 && !ranged.projectiles().empty(); ++i) ranged.update(0.05F);
        check(ranged.character(victim).health < health, "physical rounds have no artificial three-metre cutoff");

        gtc::World approach;
        const auto chaser = approach.addCharacter("Chaser", false);
        const auto runner = approach.addCharacter("Runner", false);
        place(approach, chaser, {0, 0});
        place(approach, runner, {0, 4});
        approach.attack(chaser, runner);
        check(approach.character(chaser).pendingAttack == runner && !approach.character(chaser).path.empty(),
              "single-left melee creates a go-there attack order");
        for (int i = 0; i < 240 && approach.character(chaser).pendingAttack != 0; ++i) approach.update(0.05F);
        check(approach.character(runner).health == 88.0F, "melee approach finishes with an authoritative hit");
    });

    test("advanced god paths sprint, crawl, jump, and climb", [] {
        gtc::World sprint;
        const auto runner = sprint.addCharacter("Auto Runner", false);
        place(sprint, runner, {0, 0});
        sprint.setDestination(runner, {0, 2}, true, false);
        sprint.update(0.05F);
        check(std::abs(sprint.character(runner).speed * 3.6F - 20.0F) < 0.001F,
              "Shift-RMB path uses the sprint speed");

        gtc::World crawl;
        const auto crawler = crawl.addCharacter("Advanced Crawler", false);
        place(crawl, crawler, {-7.5F, 0});
        crawl.setDestination(crawler, {-3.5F, 0}, false, true);
        bool sawProne = false;
        for (int i = 0; i < 500 && !crawl.character(crawler).path.empty(); ++i) {
            crawl.update(0.05F);
            sawProne = sawProne || crawl.character(crawler).prone;
        }
        check(sawProne && gtc::distance(crawl.character(crawler).position, {-3.5F, 0}) < 0.2F,
              "double-RMB automatically crawls through the low tunnel");

        gtc::World jump;
        const auto jumper = jump.addCharacter("Advanced Jumper", false);
        place(jump, jumper, {4.5F, 3.7F});
        jump.setDestination(jumper, {6.7F, 3.7F}, false, true);
        bool sawJump = false;
        for (int i = 0; i < 500 && !jump.character(jumper).path.empty(); ++i) {
            jump.update(0.05F);
            sawJump = sawJump || (!jump.character(jumper).grounded && jump.character(jumper).elevation > 0.15F);
        }
        check(sawJump && jump.character(jumper).elevation > 0.8F &&
              gtc::distance(jump.character(jumper).position, {6.7F, 3.7F}) < 0.2F,
              "double-RMB jumps onto the 85 centimetre crate");

        gtc::World climb;
        const auto climber = climb.addCharacter("Advanced Climber", false);
        place(climb, climber, {-8.4F, -3.8F});
        climb.setDestination(climber, {-6.6F, -3.8F}, false, true);
        bool sawGrab = false;
        for (int i = 0; i < 600 && !climb.character(climber).path.empty(); ++i) {
            climb.update(0.05F);
            sawGrab = sawGrab || climb.character(climber).grabbing;
        }
        check(sawGrab && climb.character(climber).elevation > 1.2F &&
              gtc::distance(climb.character(climber).position, {-6.6F, -3.8F}) < 0.2F,
              "double-RMB climbs the 1.25 metre crate");
    });

    test("AI uses the same multi-action toilet system", [] {
        gtc::World world;
        const auto id = world.addCharacter("Automated Dump", true);
        bool usedToilet = false;
        for (int i = 0; i < 800; ++i) {
            world.update(0.05F);
            if (world.character(id).toiletAction != gtc::ToiletAction::None) {
                usedToilet = true;
                break;
            }
        }
        check(usedToilet, "AI should autonomously request and use a toilet action");
    });

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All simulation tests passed. Biology is temporarily under control.\n";
    return 0;
}
