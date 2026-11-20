#pragma once

#include "client/skeleton_toe_editor.h"
#include "client/skeleton_toe_surface.h"
#include <map>

namespace gtc::native {
// Mesh-coordinate attachments supplied by the user on the CT surface.
// These are landmarks, not instructions to move or rotate a bone.
struct ToeSurfaceMark { std::array<Vec3,2> end{}; unsigned placed=0; };
using ToeSurfaceMarks=std::map<std::string,ToeSurfaceMark>;

} // namespace gtc::native
