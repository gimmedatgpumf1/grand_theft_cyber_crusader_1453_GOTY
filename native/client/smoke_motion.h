// The motion checks: the smoke test's look at what a rendered frame is allowed to
// do when the state it draws has changed, and at what it must not do when the
// state has not. They need a renderer and the capture hook, and nothing else, so
// they live in their own file.

#ifndef GTC_CLIENT_SMOKE_MOTION_H
#define GTC_CLIENT_SMOKE_MOTION_H

#include <string>

namespace gtc::native {
class WorldRenderer;
}

// Runs every motion case against `renderer`, and on failure writes what went
// wrong into `error`.
bool motionChecks(gtc::native::WorldRenderer& renderer, std::string& error);

#endif
