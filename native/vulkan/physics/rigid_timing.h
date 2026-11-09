#pragma once

namespace gtc::native::gpu_physics {
// The game server ticks every 50 ms. Local room physics uses twelve smaller
// fixed steps per server-tick interval; it is not yet server-authoritative.
inline constexpr unsigned kServerTickHz=20;
inline constexpr unsigned kSubstepsPerTick=12;
inline constexpr unsigned kPhysicsHz=kServerTickHz*kSubstepsPerTick;
inline constexpr float kPhysicsStep=1.0f/float(kPhysicsHz);
} // namespace gtc::native::gpu_physics
