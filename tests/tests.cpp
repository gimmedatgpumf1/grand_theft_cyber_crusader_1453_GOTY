#include "core/collision.h"
#include "core/json.h"
#include "core/pathfinding.h"
#include "core/world.h"

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

    test("shitting drains bowel without magically emptying bladder", [] {
        gtc::World world;
        const auto id = world.addCharacter("Bowel Subject", false);
        moveToToilet(world, id);
        world.adjustNeed(id, "bowel", 100);
        const float bladder = world.character(id).needs.bladder;
        world.requestToilet(id, "shit");
        check(world.character(id).onToilet, "shitting is seated");
        for (int i = 0; i < 200 && world.character(id).toiletAction != gtc::ToiletAction::None; ++i) {
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
        for (int i = 0; i < 200 && world.character(id).toiletAction != gtc::ToiletAction::None; ++i) {
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

    test("first-person style piss action drains bladder and makes puddles", [] {
        gtc::World world;
        const auto id = world.addCharacter("Postal Worker", false);
        const float before = world.character(id).needs.bladder;
        world.setInput(id, {}, false, true, 0.0F, -0.2F);
        for (int i = 0; i < 20; ++i) world.update(0.05F);
        const auto after = world.character(id);
        check(after.pissing, "pissing state is active");
        check(after.needs.bladder < before - 10.0F, "bladder drains");
        check(world.snapshot().find("\"puddles\":[{\"id\":") != std::string::npos,
              "urine creates persistent world puddles");
        world.setInput(id, {}, false, false, 0.0F, 0.0F);
        world.update(0.05F);
        check(!world.character(id).pissing, "releasing input stops pissing");
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
