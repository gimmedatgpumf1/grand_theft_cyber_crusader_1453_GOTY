#include "client/fps_report.h"

#include <chrono>
#include <cstdio>
#include <cstring>

int main() {
    using Reporter = gtc::native::FrameRateReporter;
    using namespace std::chrono_literals;
    std::FILE* output = std::tmpfile();
    if (!output) return 1;
    const Reporter::Clock::time_point start{};
    Reporter reporter(start);
    reporter.record(start + 500ms, 2, 4, 6, output);
    if (std::ftell(output) != 0) return 1;
    reporter.record(start + 1100ms, 4, 6, 8, output);
    std::fflush(output);
    std::rewind(output);
    char line[256]{};
    bool pass = std::fgets(line, sizeof(line), output) &&
        std::strcmp(line, "[fps] 1.8 (550.00 ms/frame: draw 3.00, present 5.00, logic 7.00)\n") == 0;
    std::fseek(output, 0, SEEK_END);
    reporter.record(start + 1600ms, 1, 2, 3, output);
    reporter.record(start + 2100ms, 3, 4, 5, output);
    std::fflush(output);
    std::rewind(output);
    std::fgets(line, sizeof(line), output);
    pass = pass && std::fgets(line, sizeof(line), output) &&
        std::strcmp(line, "[fps] 2.0 (500.00 ms/frame: draw 2.00, present 3.00, logic 4.00)\n") == 0;
    std::fclose(output);
    std::puts(pass ? "default frame reporting: PASS" : "default frame reporting: FAIL");
    return pass ? 0 : 1;
}
