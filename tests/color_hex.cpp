#include "client/raster.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

using gtc::native::colorHex;
using gtc::native::hexRgb;

static_assert(hexRgb("#000000") == 0x000000);
static_assert(hexRgb("#ffffff") == 0xffffff);
static_assert(hexRgb("#ABCDEF") == 0xabcdef);
static_assert(hexRgb("#012345") == 0x012345);
static_assert(hexRgb("#d7ff54") == 0xd7ff54);

int main() {
    // Compare the compile-time RGB path to the old dynamic-string path. Each
    // channel spans all 256 byte values, including lowercase and uppercase.
    for (unsigned i=0; i<65536; ++i) {
        const std::uint32_t rgb=((i&255U)<<16)|(((i>>8)&255U)<<8)|((i*37U)&255U);
        char text[8];
        std::snprintf(text,sizeof text,(i&1U)?"#%06X":"#%06x",rgb);
        const auto old=colorHex(text), current=colorHex(rgb);
        assert(old.r==current.r && old.g==current.g && old.b==current.b);
    }
    std::puts("Compile-time colour parsing and dynamic colours: PASS");
}
