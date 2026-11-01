#pragma once

#include "native.h"

#include "client/raster.h"
#include "core/types.h"

#include <functional>

namespace gtc::native {

// The authored stand-in for the imported human: a deliberately coarse figure
// built out of boxes, in the spirit of the 2000-era shooter it takes its
// silhouette from. Three things make it the model of record:
//
//   * every vertex belongs to exactly one bone, so skinning is a rigid
//     transform and cannot smear a limb across a joint;
//   * the whole body is a few hundred triangles instead of nineteen thousand,
//     which is where the frame time went;
//   * the joints are the same names the pose code already speaks, so walking,
//     crouching, sitting, aiming and the stream all keep working untouched.
//
// The mesh is built in metres with the soles at y = 0 and the crown at 1.8,
// then expressed in the source units the runtime transform expects.
HumanModel blockBody(bool male);

// Head dressing for the authored body: the hair block and the sunglasses. Both
// are drawn from the pose of the head bone, so they follow a nod or a turn the
// same way the imported strand meshes do. `headPoint` maps a point in head
// space to the world.
// The two sliders the block body actually answers to. Stature scales the whole
// figure from the soles up, and head size scales only the skull and its face
// panels. Both are expressed as multipliers around their rest values.
struct BlockProportions {
    double stature = 1.0;
    double head = 1.0;
};
BlockProportions blockProportions(const HumanModel& model, const std::vector<double>& morphs);

// Applies the proportions to a stock of rest-space points. Kept separate from
// the builder so the renderer can reshape the cached mesh without rebuilding
// the model.
void shapeBlockBody(const BlockProportions& proportions, std::vector<Vec3>& points);

void drawBlockAccessories(Rasterizer& rasterizer, const Camera& camera,
                          const std::function<Vec3(Vec3)>& headPoint, const Character& character);

}  // namespace gtc::native
