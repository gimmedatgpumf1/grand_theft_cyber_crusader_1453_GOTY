#pragma once

#include "client/raster.h"

#include <algorithm>

namespace gtc::native {

// The two colour tables both body systems paint with. They live in one header
// so the imported mesh and the authored block body cannot drift apart: a skin
// tone means the same thing to the creator, the HUD portrait and the world.

inline Color skinColor(int index) {
    static const char* colors[] = {"#f0c8a8", "#d9a17d", "#b97c58", "#8a573e", "#714a38", "#e5b894"};
    return colorHex(colors[std::clamp(index, 0, 5)]);
}

inline Color hairColor(int index) {
    static const char* colors[] = {"#171a1c", "#3b281d", "#75604a", "#b5a075", "#5b6268",
                                   "#69362c", "#e5d5aa", "#d2b477", "#a9adb0"};
    return colorHex(colors[std::clamp(index, 0, 8)]);
}

}  // namespace gtc::native
