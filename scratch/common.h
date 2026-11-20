// Shared setup for the probes in this directory: the room, opened up so a probe can
// read the numbers the physics is actually working with.
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#define private public
#include "client/playground_pair.h"
#include "client/playground_ball_shape.h"
#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#undef private
using namespace gtc::native;
static double gapToBody(PlaygroundRoom& r, int i) {
    return ball_shape::outlineGap(r.ball_.position, r.bodies_[i].position.data(),
                                  r.bodies_[i].position.data(), r.bodies_[i].count, 1.0).gap;
}
