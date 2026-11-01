// GCC 12 reports 202100L in C++23 mode. MSVC 14.52 is the first
// toolset to support /std:c++23; older tools may silently ignore the switch.
#include <cmath>
static_assert(std::sqrt(4.0) == 2.0, "C++23 constexpr cmath support is required");
#if defined(_MSC_VER) && !defined(__clang__)
static_assert(_MSC_VER >= 1952, "MSVC 14.52 or newer is required for /std:c++23");
static_assert(_MSVC_LANG >= 202302L, "Build with /std:c++23");
#else
static_assert(__cplusplus > 202002L, "Build with -std=c++23");
#endif
int main() { return 0; }
