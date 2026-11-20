// The parser, the wire, the collision resolve and the route search.

#include "harness.h"

#ifndef _WIN32
#include <sys/socket.h>
#endif

namespace {

void runJSONInputParser() {
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
}

void runLargeCombatSnapshotsSurvive() {
    test("large combat snapshots survive native websocket transport", [] {
#ifndef _WIN32
        int sockets[2] = {-1, -1};
        const bool pairReady = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0;
        check(pairReady, "create websocket test pair");
        if (!pairReady) return;
        const std::string payload(128 * 1024, 'x');
        const bool sent = net::sendWebSocket(sockets[0], payload);
        std::string decoded;
        int opcode = 0;
        const bool received = sent && net::receiveWebSocket(sockets[1], decoded, opcode, false);
        net::closeSocket(sockets[0]);
        net::closeSocket(sockets[1]);
        check(sent && received && opcode == 1 && decoded == payload,
              "a 128 KiB world snapshot no longer crosses a silent 64 KiB disconnect limit");
#endif
    });
}

void runCollisionSlidesInsteadOf() {
    test("collision slides instead of crossing objects", [] {
        const std::vector<gtc::Obstacle> obstacles{{{{0, 0}, {1, 1}, 2, "crate"}}};
        const gtc::Vec2 result = gtc::moveWithCollisions({-2, 0}, {4, 0.4F}, 0.35F,
                                                          obstacles, 5, 5);
        check(result.x < -1.3F, "body must remain on the near side");
        check(result.y > 0.3F, "unblocked axis should still slide");
        check(!gtc::circleHitsWorld(result, 0.35F, obstacles, 5, 5), "resolved body is valid");
    });
}

void runAPathsAroundA() {
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
}

} // namespace

void tests_core() {
    runJSONInputParser();
    runLargeCombatSnapshotsSurvive();
    runCollisionSlidesInsteadOf();
    runAPathsAroundA();
}
