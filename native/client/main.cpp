#include "client/client.h"
#include "client/interactive.h"
#include "client/renderer.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>

namespace {

double number(const std::string& value, double fallback = 0) {
    try { return std::stod(value); } catch (...) { return fallback; }
}

} // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1", name = "Native Citizen", root = ".";
    std::string output = "build/native-client/frame.png", mode = "tps", toilet, reviewUi;
    int port = 1453, frames = 1, benchmark = 1;
    bool path = false, move = false, headless = false;
    gtc::Vec2 destination{}, movement{};
    gtc::native::ViewState view;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--headless") headless = true;
        else if (argument == "--review-ui" && i + 1 < argc) reviewUi = argv[++i];
        else if (argument == "--host" && i + 1 < argc) host = argv[++i];
        else if (argument == "--port" && i + 1 < argc) port = static_cast<int>(number(argv[++i], 1453));
        else if (argument == "--name" && i + 1 < argc) name = argv[++i];
        else if (argument == "--root" && i + 1 < argc) root = argv[++i];
        else if (argument == "--output" && i + 1 < argc) output = argv[++i];
        else if (argument == "--frames" && i + 1 < argc) frames = static_cast<int>(number(argv[++i], 1));
        else if (argument == "--benchmark" && i + 1 < argc) benchmark = std::max(1, static_cast<int>(number(argv[++i], 1)));
        else if (argument == "--width" && i + 1 < argc) view.width = std::max(1, static_cast<int>(number(argv[++i], gtc::native::kDefaultWindowWidth)));
        else if (argument == "--height" && i + 1 < argc) view.height = std::max(1, static_cast<int>(number(argv[++i], gtc::native::kDefaultWindowHeight)));
        else if (argument == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (argument == "--path" && i + 2 < argc) {
            destination = {static_cast<float>(number(argv[++i])), static_cast<float>(number(argv[++i]))};
            path = true;
        } else if (argument == "--move" && i + 2 < argc) {
            movement = {static_cast<float>(number(argv[++i])), static_cast<float>(number(argv[++i]))};
            move = true;
        } else if (argument == "--toilet" && i + 1 < argc) toilet = argv[++i];
        else if (argument == "--yaw" && i + 1 < argc) view.yaw = static_cast<float>(number(argv[++i]));
        else if (argument == "--pitch" && i + 1 < argc) view.pitch = static_cast<float>(number(argv[++i]));
        else {
            std::cerr << "Usage: gtc-native-client [--headless] [--host HOST] [--port 1453] [--name NAME] [--frames N]\n"
                         "  [--mode tps|fps|god] [--path X Z] [--move X Z] [--toilet sit|shit|piss]\n"
                         "  [--review-ui DIR] (render native overlays without a display)\n"
                         "  [--width PIXELS] [--height PIXELS] [--benchmark N]\n"
                         "  [--yaw RADIANS] [--pitch RADIANS] [--output PNG]\n";
            return 2;
        }
    }
    if (mode == "fps") view.mode = gtc::native::ViewMode::FirstPerson;
    else if (mode == "god") view.mode = gtc::native::ViewMode::God;
    else view.mode = gtc::native::ViewMode::ThirdPerson;
    if (!headless || !reviewUi.empty()) {
        gtc::native::InteractiveOptions options;
        options.host = host;
        options.port = static_cast<std::uint16_t>(port);
        options.name = name;
        options.root = root;
        if (!reviewUi.empty()) return gtc::native::runInteractiveReview(options, reviewUi);
        return gtc::native::runInteractive(options);
    }
    std::string error;
    gtc::native::NativeClient client;
    if (!client.connect(host, static_cast<std::uint16_t>(port), error) || !client.sendJoin(name)) {
        std::cerr << error << '\n';
        return 1;
    }
    if (path) client.sendPath(destination);
    if (!toilet.empty()) client.sendToilet(toilet); // Strict server range check; never an implicit path.
    gtc::native::WorldRenderer renderer;
    if (!renderer.load(root, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    for (int frame = 0; frame < frames; ++frame) {
        if (move) client.sendInput(movement, false, false, view.yaw, view.pitch);
        if (!client.receive(error)) {
            std::cerr << error << '\n';
            return 1;
        }
    }
    gtc::native::NativeFrame frame;
    if (!renderer.render(client.world(), client.state(), client.playerId(), view, frame, error)) {
        std::cerr << "Native render failed: " << error << '\n';
        return 1;
    }
    const auto benchmarkStart = std::chrono::steady_clock::now();
    for (int iteration = 1; iteration < benchmark; ++iteration) {
        if (!renderer.render(client.world(), client.state(), client.playerId(), view, frame, error)) {
            std::cerr << "Native render failed: " << error << '\n';
            return 1;
        }
    }
    if (benchmark > 1) {
        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - benchmarkStart).count() / (benchmark - 1);
        std::cout << "native render: " << milliseconds << " ms/frame at " << view.width << "x" << view.height << '\n';
    }
    if (!frame.save(output)) {
        std::cerr << "Native render failed: cannot save " << output << '\n';
        return 1;
    }
    std::cout << "native client: id=" << client.playerId() << " tick=" << client.state().tick
              << " citizens=" << client.state().characters.size() << " frame=" << output << '\n';
    return 0;
}
