#pragma once

// The little harness the core tests are written against.
//
// It is a header because the tests are no longer one file: a thousand lines of
// checks in a single translation unit is a file nobody can find anything in, and
// the build refuses files that size anyway. Every test file includes this and
// nothing else has to know how a check is reported.

#include "core/collision.h"
#include "core/json.h"
#include "core/pathfinding.h"
#include "core/projectile.h"
#include "core/world.h"
#include "net/websocket.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace test_support {

inline int failures = 0;

inline void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "  FAIL: " << message << '\n';
        ++test_support::failures;
    }
}

inline void test(const std::string& name, const std::function<void()>& body) {
    std::cout << "[test] " << name << '\n';
    body();
}

inline void moveToToilet(gtc::World& world, std::uint32_t id) {
    world.setDestination(id, {0, -2.85F});
    for (int i = 0; i < 600 && !world.character(id).path.empty(); ++i) world.update(0.05F);
    check(gtc::distance(world.character(id).position, {0, -2.85F}) < 0.2F,
          "explicit movement should reach the toilet");
}

inline bool driveTo(gtc::World& world, std::uint32_t id, const gtc::Vec2& target) {
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

inline void place(gtc::World& world, std::uint32_t id, const gtc::Vec2& target) {
    check(driveTo(world, id, target), "test setup position is reachable");
}

}  // namespace test_support

// The tests are written in the room's own voice: check, test, place, driveTo.
using test_support::check;
using test_support::driveTo;
using test_support::moveToToilet;
using test_support::place;
using test_support::test;
