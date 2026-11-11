// How a body moves: facing, measured speeds, stances, jumps and steps.

#include "harness.h"

namespace {

void runToiletRangePersistentSitting() {
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
}

void runManualBackwardMovementPreserves() {
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
}

void runPlayerAndNPCLocomotion() {
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
}

void runCrouchAndProneStances() {
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
}

void runJumpApexChargedJump() {
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
}

void runAutomaticAndSlowSteps() {
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
        bool exactRiseSlow = false, roseFromLow = false;
        float previousElevation = boundaryWorld.character(boundary).elevation;
        for (int i = 0; i < 50; ++i) {
            boundaryWorld.setInput(boundary, {0, -1}, false, false, 3.14159265F, 0);
            boundaryWorld.update(0.05F);
            const auto& c = boundaryWorld.character(boundary);
            exactRiseSlow = exactRiseSlow || (c.grounded && std::abs(c.elevation - 0.18F) < 0.001F &&
                c.position.y < -1.0F && c.speed <= 0.7501F && c.speed > 0.0F);
            roseFromLow = roseFromLow || (std::abs(previousElevation - 0.18F) < 0.001F &&
                std::abs(c.elevation - 0.45F) < 0.001F);
            previousElevation = c.elevation;
        }
        check(exactRiseSlow && roseFromLow,
              "an exact 27 centimetre rise uses current altitude rather than assuming ground");
    });
}

void runToiletGrabClimbRelease() {
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
}

void runOrdinaryCrateUsesThe() {
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
}

} // namespace

void tests_character() {
    runToiletRangePersistentSitting();
    runManualBackwardMovementPreserves();
    runPlayerAndNPCLocomotion();
    runCrouchAndProneStances();
    runJumpApexChargedJump();
    runAutomaticAndSlowSteps();
    runToiletGrabClimbRelease();
    runOrdinaryCrateUsesThe();
}
