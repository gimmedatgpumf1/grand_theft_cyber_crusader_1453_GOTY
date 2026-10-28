#pragma once

#include "client/renderer.h"

#include <string>

namespace gtc::native {

// The benchmark is the diagnostic; it should not need a second environment
// variable to say which stage it is timing.
void requireProfiling();
double nowMs();
void reportLine(const char* label, double milliseconds);

// One skull through every backend, then the whole skeleton through the one that
// matters, then the same frames with the copy back removed.
int runSkullBenchmark(const std::string& root, ViewState view, const std::string& renderer, int iterations);

} // namespace gtc::native
