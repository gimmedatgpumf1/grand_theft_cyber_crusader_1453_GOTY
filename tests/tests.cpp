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

    test("toilet auto-path, range, and both needs", [] {
        gtc::World world;
        const auto id = world.addCharacter("Test Subject", false);
        world.requestToilet(id);
        bool satDown = false;
        for (int i = 0; i < 600; ++i) {
            world.update(0.05F);
            if (world.character(id).onToilet) {
                satDown = true;
                break;
            }
        }
        check(satDown, "citizen should auto-path into interaction range and sit");
        for (int i = 0; i < 200 && world.character(id).onToilet; ++i) world.update(0.05F);
        const auto relieved = world.character(id);
        check(!relieved.onToilet, "toilet action should finish");
        check(relieved.needs.bladder == 0.0F && relieved.needs.bowel == 0.0F,
              "throne empties bladder and bowel");
        world.requestToilet(id);
        check(world.character(id).onToilet, "throne can be reused from interaction range");
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

    test("AI uses exactly the same toilet interaction", [] {
        gtc::World world;
        const auto id = world.addCharacter("Automated Dump", true);
        bool usedToilet = false;
        for (int i = 0; i < 800; ++i) {
            world.update(0.05F);
            if (world.character(id).onToilet) {
                usedToilet = true;
                break;
            }
        }
        check(usedToilet, "AI should autonomously request and use the toilet");
    });

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All simulation tests passed. Biology is temporarily under control.\n";
    return 0;
}
