#pragma once

// The actual SDK's Winsock 1 and Winsock 2 declarations cannot coexist.
#ifdef TEST_WINSOCK1_INCLUDED
#error "Include winsock2.h before windows.h"
#endif
#define TEST_WINSOCK2_INCLUDED
#include <cstdint>
using SOCKET = std::uintptr_t;
#define INVALID_SOCKET (~SOCKET{0})
