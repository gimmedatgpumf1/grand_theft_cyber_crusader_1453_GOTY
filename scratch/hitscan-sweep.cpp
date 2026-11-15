// The room's answer to the fastest arrivals, as a table.
//
// 135 shots: every shape in the room, sixteen aims at it, five speeds from a hand's
// throw to a hundred kilometres a second. For each one it writes the deepest the ball
// ever got inside a shape -- tick by tick, not only where it ended up -- and whether
// any number in the room stopped being a number. The velocities are logged as log2 as
// well as themselves, because what is being tested is an order of magnitude and a
// factor, not a difference.
//
// Build it by hand, as the other probes here are:
//
//     g++ -pthread -std=c++20 -O2 -Iscratch -Isrc -Inative -Inative/client \
//         scratch/hitscan-sweep.cpp build/obj/native-client/playground*.o \
//         -o build/bin/hitscan-sweep
//
// (leave out playground_room_draw.o -- it wants the rasterizer, which this does not).
// It writes scratch/hitscan-100km.csv.

#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private

#include <cmath>
#include <cstdio>

using namespace gtc::native;

int main(int argc, char** argv) {
    FILE* out = std::fopen(argc > 1 ? argv[1] : "scratch/hitscan-100km.csv", "w");
    if (!out) {
        std::printf("could not write the table\n");
        return 1;
    }
    std::fprintf(out,
                 "shape,aim_deg,speed_m_per_s,log2_speed,worst_inside_m,log2_worst_inside,non_finite,"
                 "ball_x,ball_y,ball_speed\n");
    const double speeds[] = {1.0, 6.0, 40.0, 1000.0, 100000.0};
    double worstAll = 0.0;
    int nonFinite = 0;
    int shots = 0;
    for (std::size_t shape = 0; shape < PlaygroundRoom::kBodyCount; ++shape) {
        for (int aim = 0; aim < 16; ++aim) {
            const double angle = -3.141592653589793 + 6.283185307179586 * aim / 16.0;
            for (const double speed : speeds) {
                PlaygroundRoom room;
                room.reset();
                for (int tick = 0; tick < 400; ++tick) room.advance(1.0 / 20.0);
                const Vec3 middle = room.bodyMiddle(shape);
                const Vec3 start{middle.x + 1.2 * std::cos(angle), middle.y + 1.2 * std::sin(angle), 0};
                if (!room.ballPlacementClear(start)) continue;
                room.ball_.position = start;
                room.ball_.velocity = Vec3{-std::cos(angle), -std::sin(angle), 0} * speed;
                room.ball_.spin = 0.0;
                room.ball_.frozen = false;
                double worst = 0.0;
                bool gone = false;
                for (int tick = 0; tick < 120; ++tick) {
                    room.advance(1.0 / 20.0);
                    for (std::size_t index = 0; index < PlaygroundRoom::kBodyCount; ++index) {
                        Vec3 way{}, at{};
                        worst = std::max(worst, -room.bodyBallGap(index, way, at));
                    }
                    if (!std::isfinite(room.ball_.position.x) || !std::isfinite(room.ball_.velocity.x)) {
                        gone = true;
                        break;
                    }
                }
                ++shots;
                if (gone) ++nonFinite;
                worstAll = std::max(worstAll, worst);
                std::fprintf(out, "%zu,%.1f,%.0f,%.3f,%.6e,%.3f,%d,%.6f,%.6f,%.6e\n", shape,
                             angle * 180.0 / 3.141592653589793, speed, std::log2(speed), worst,
                             worst > 0.0 ? std::log2(worst) : -1000.0, gone ? 1 : 0, room.ball_.position.x,
                             room.ball_.position.y, length(room.ball_.velocity));
            }
        }
    }
    std::fclose(out);
    std::printf("%d shots, %d of them not a number, worst %.6e m inside (log2 %.3f)\n", shots, nonFinite, worstAll,
                worstAll > 0.0 ? std::log2(worstAll) : -1000.0);
    return 0;
}
