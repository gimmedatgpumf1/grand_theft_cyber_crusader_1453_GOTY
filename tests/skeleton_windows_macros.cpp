// Windows headers define near/far as macros; RPC headers can define small.
// Load the real headers first on Windows, then ensure the same macro conditions
// are exercised on other platforms before the comparison headers are parsed.
#include "client/skeleton_body.h"
#include <numeric>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#endif
#ifndef near
#define near
#endif
#ifndef far
#define far
#endif
#ifndef small
#define small char
#endif
#include "client/skeleton_room.h"
#include "client/skeleton_toe_yaw.h"
#include "client/skeleton_comparison_ui.h"

#ifndef near
#error "The public headers must not undefine the Windows near macro"
#endif
#ifndef far
#error "The public headers must not undefine the Windows far macro"
#endif
#ifndef small
#error "The public headers must not undefine the Windows small macro"
#endif

int main() {
    using namespace gtc::native;
    const std::vector<Vec3> points{{0,0,0},{1,10,0},{2,10,0},{2.1,0,0},{5,0,0}};
    const bilateral::PointTree tree(points);
    // The first query must search the opposite branch as well as the nearer one.
    const Vec3 across=tree.nearest({1.9,0,0});
    const Vec3 nearby=tree.nearest({0.1,0,0});
    return across.x==2.1 && nearby.x==0 ? 0 : 1;
}
