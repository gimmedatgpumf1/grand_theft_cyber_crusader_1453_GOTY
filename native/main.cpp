#include "native.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

std::string metricsJson(const std::vector<gtc::native::FitResult>& results) {
    std::ostringstream output;
    output << std::setprecision(10) << "{\n";
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        output << "  \"" << gtc::jsonEscape(result.preset.key) << "\": {"
               << "\"initialScore\":" << result.initialScore << ",\"score\":" << result.score
               << ",\"evaluations\":" << result.evaluations << ",\"milliseconds\":" << result.milliseconds
               << ",\"iterations\":" << result.iterations << "}"
               << (i + 1 == results.size() ? "\n" : ",\n");
    }
    output << "}\n";
    return output.str();
}

void usage() {
    std::cout << "native-fit [--root PATH] [--iterations N] [--direct] [--output PATH] [--render-only] [--no-render] [names...]\n"
              << "Names: marilyn ibrahim xi putin trump taylor obama\n";
}

} // namespace

int main(int argc, char** argv) {
    using namespace gtc::native;
    std::string root = std::filesystem::current_path().string();
    std::vector<std::string> selected;
    int iterations = 8;
    bool render = true;
    bool renderOnly = false;
    bool useSurface = true;
    std::string outputDirectory;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") { usage(); return 0; }
        if (argument == "--root" && i + 1 < argc) root = argv[++i];
        else if (argument == "--iterations" && i + 1 < argc) iterations = std::max(0, std::atoi(argv[++i]));
        else if (argument == "--direct") useSurface = false;
        else if (argument == "--output" && i + 1 < argc) outputDirectory = argv[++i];
        else if (argument == "--render-only") renderOnly = true;
        else if (argument == "--no-render") render = false;
        else if (!argument.empty() && argument[0] == '-') { usage(); return 2; }
        else selected.push_back(argument);
    }
    std::string error;
    HumanModel male;
    if (!male.load(root, true, error)) {
        std::cerr << "native-fit: " << error << '\n';
        return 1;
    }
    std::vector<Preset> presets = loadPresets(root, male, error);
    if (!error.empty() || presets.empty()) {
        std::cerr << "native-fit: " << error << '\n';
        return 1;
    }
    auto references = loadReferences(root, error);
    if (!error.empty()) {
        std::cerr << "native-fit: " << error << '\n';
        return 1;
    }
    HumanModel female;
    bool femaleLoaded = false;
    std::vector<Preset> finalPresets;
    std::vector<FitResult> results;
    if (outputDirectory.empty()) outputDirectory = root + "/build/native-fit";
    std::error_code code;
    std::filesystem::create_directories(outputDirectory, code);
    if (!writeText(outputDirectory + "/initial-presets.json", presetsJson(presets), error)) {
        std::cerr << error << '\n';
        return 1;
    }
    for (const Preset& preset : presets) {
        if (!selected.empty() && std::find(selected.begin(), selected.end(), preset.key) == selected.end()) continue;
        const HumanModel* model = &male;
        if (!preset.male) {
            if (!femaleLoaded) {
                if (!female.load(root, false, error)) {
                    std::cerr << "native-fit: " << error << '\n';
                    return 1;
                }
                femaleLoaded = true;
            }
            model = &female;
        }
        const auto reference = references.find(preset.key);
        if (reference == references.end()) {
            std::cerr << "native-fit: no reference for " << preset.key << '\n';
            return 1;
        }
        std::cout << "fit " << preset.key << " (" << (preset.male ? "male" : "female") << ")\n" << std::flush;
        FitResult result;
        result.preset = preset;
        if (!renderOnly) result = fitPortrait(*model, reference->second, preset, iterations, true, useSurface);
        if (render) {
            if (!renderPortrait(*model, preset, root, outputDirectory + "/" + preset.key + "-initial.png", 480, 640, error) ||
                !renderPortrait(*model, result.preset, root, outputDirectory + "/" + preset.key + "-final.png", 480, 640, error)) {
                std::cerr << "native-fit: " << error << '\n';
                return 1;
            }
        }
        std::cout << "  done " << preset.key << " initial " << result.initialScore << " final " << result.score
                  << " evaluations " << result.evaluations << " ms " << result.milliseconds << '\n';
        finalPresets.push_back(result.preset);
        results.push_back(result);
        if (!writeText(outputDirectory + "/presets.json", presetsJson(finalPresets), error) ||
            !writeText(outputDirectory + "/results.json", metricsJson(results), error)) {
            std::cerr << "native-fit: " << error << '\n';
            return 1;
        }
    }
    std::cout << "native-fit complete: " << results.size() << " people, 480x640 review renders\n";
    return 0;
}
