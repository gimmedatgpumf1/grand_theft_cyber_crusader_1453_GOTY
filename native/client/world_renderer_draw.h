// The room and the things in it, drawn.
//
// world_renderer.cpp decides what a frame looks like: the camera, which
// characters need a new pose, and the order things are drawn in. The drawing
// itself is here, in two files, because a room with a floor, dressing, obstructions,
// water, puddles and projectiles in it is not one subject:
//
//   world_renderer_room.cpp     the floor, the signs, the dressing, the toilet
//   world_renderer_effects.cpp  paths, water, puddles, projectiles
//
// These are the same functions that used to sit in an anonymous namespace in
// world_renderer.cpp, given names and a header so both files can see them.

#ifndef GTC_CLIENT_WORLD_RENDERER_DRAW_H
#define GTC_CLIENT_WORLD_RENDERER_DRAW_H

#include "client/renderer.h"
#include "client/human_renderer.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace gtc::native::world_draw {

// The floor, and the colour its tiles are laid down in.
const Texture& floorTexture();
void drawFloor(Rasterizer& rasterizer, const Camera& camera, const ClientWorld& world);

// Quadring primitives the room and its furniture are built out of.
void drawQuad(Rasterizer& rasterizer, const Camera& camera, const std::array<Vec3, 4>& points,
              Vec3 normal, const Material& material);
void drawRing(Rasterizer& rasterizer, const Camera& camera, Vec3 center, double inner, double outer,
              const Material& material, int segments = 40);
void drawOctahedron(Rasterizer& rasterizer, const Camera& camera, Vec3 center, double radius,
                    const Material& material);

// Signs, dressed walls, the porcelain, and the obstructions the pathfinder knows about.
const Texture& signTexture(const std::string& text, const char* foreground, const char* background);
void drawSign(Rasterizer& rasterizer, const Camera& camera, Vec3 center, double width, double height,
              const Texture& texture);
void drawDressing(Rasterizer& rasterizer, const Camera& camera, const ClientWorld& world,
                  const ClientState& state, const ViewState& view);
void drawBowl(Rasterizer& rasterizer, const Camera& camera, Vec3 center, const Material& porcelain);
void drawToilet(Rasterizer& rasterizer, const Camera& camera, gtc::Vec2 toilet);
void drawObstacles(Rasterizer& rasterizer, const Camera& camera, const ClientWorld& world);

// The things that move or flow.
void drawPaths(Rasterizer& rasterizer, const Camera& camera, const ClientState& state, std::uint32_t localId);
void drawWater(Rasterizer& rasterizer, const Camera& camera, const ClientWorld& world,
               const ClientState& state, double time);
void drawPuddles(Rasterizer& rasterizer, const Camera& camera, const ClientState& state);
void drawProjectiles(Rasterizer& rasterizer, const Camera& camera, const ClientState& state,
                     const std::unordered_map<std::uint32_t, HumanRenderCache>& humanCaches);

}  // namespace gtc::native::world_draw

#endif
