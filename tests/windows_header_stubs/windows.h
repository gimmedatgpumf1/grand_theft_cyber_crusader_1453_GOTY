#pragma once

// Compile-test model of the SDK include-order rule, not a Windows API header.
// A non-lean windows.h loads Winsock 1 unless Winsock 2 was already included.
#if !defined(TEST_WINSOCK2_INCLUDED) && !defined(WIN32_LEAN_AND_MEAN)
#define TEST_WINSOCK1_INCLUDED
#endif
#define near
#define far
#define small char
