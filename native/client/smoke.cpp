#include "client/client.h"
#include "client/renderer.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int integer(const std::string& value, int fallback) {
    try { return std::stoi(value); } catch (...) { return fallback; }
}

bool receivePair(gtc::native::NativeClient& a, gtc::native::NativeClient& b, std::string& error) {
    return a.receive(error) && b.receive(error);
}

} // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1", output = "build/native-smoke", root = ".";
    int port = 1453;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--host" && i + 1 < argc) host = argv[++i];
        else if (argument == "--port" && i + 1 < argc) port = integer(argv[++i], 1453);
        else if (argument == "--output" && i + 1 < argc) output = argv[++i];
        else if (argument == "--root" && i + 1 < argc) root = argv[++i];
        else {
            std::cerr << "Usage: gtc-native-smoke [--host 127.0.0.1] [--port 1453] [--output DIR]\n";
            return 2;
        }
    }
    std::string error;
    gtc::native::NativeClient alpha, beta;
    if (!alpha.connect(host, static_cast<std::uint16_t>(port), error) ||
        !beta.connect(host, static_cast<std::uint16_t>(port), error)) {
        std::cerr << error << '\n';
        return 1;
    }
    gtc::Appearance aAppearance;
    aAppearance.male = true;
    aAppearance.skin = 1;
    aAppearance.hair = 2;
    aAppearance.hairColor = 1;
    aAppearance.shirt = 1;
    aAppearance.coat = true;
    aAppearance.suit = true;
    aAppearance.tie = 2;
    aAppearance.morphs[0] = 35;
    aAppearance.morphs[3] = 8;
    gtc::Appearance bAppearance;
    bAppearance.male = false;
    bAppearance.skin = 3;
    bAppearance.hair = 5;
    bAppearance.hairColor = 3;
    bAppearance.shirt = 2;
    bAppearance.pants = 1;
    bAppearance.dress = true;
    bAppearance.morphs[0] = 28;
    bAppearance.morphs[7] = 12;
    if (!alpha.sendJoin("Native Alpha") || !beta.sendJoin("Native Beta") ||
        !alpha.sendCustomize(aAppearance) || !beta.sendCustomize(bAppearance) ||
        !alpha.sendCheat("bowel", 100) || !beta.sendCheat("bladder", 100) ||
        !alpha.sendPath(alpha.world().toiletFront) || !beta.sendInput({0, -1}, false, false, 3.14159265F, 0)) {
        std::cerr << "Could not send native smoke commands\n";
        return 1;
    }
    bool replicated = false, customized = false, inputMoved = false, pathSeen = false;
    bool toiletRequested = false, toiletSeen = false;
    int iterations = 0;
    for (; iterations < 100; ++iterations) {
        if (!receivePair(alpha, beta, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        if (iterations == 5) {
            if (!beta.sendInput({}, false, false, 0, 0) || !beta.sendPath({3.0, 2.3})) return 1;
        }
        const gtc::Character* a = alpha.state().character(alpha.playerId());
        const gtc::Character* b = beta.state().character(beta.playerId());
        const gtc::Character* aInB = beta.state().character(alpha.playerId());
        const gtc::Character* bInA = alpha.state().character(beta.playerId());
        replicated = a && b && aInB && bInA && aInB->name == "Native Alpha" && bInA->name == "Native Beta";
        customized = a && b && aInB && bInA && a->appearance.morphs[0] == 35 && a->appearance.morphs[3] == 8 &&
                     !b->appearance.male && b->appearance.hair == 5 && b->appearance.morphs[7] == 12 &&
                     aInB->appearance.coat && aInB->appearance.suit && aInB->appearance.tie == 2 &&
                     bInA->appearance.dress && bInA->appearance.morphs[7] == 12;
        inputMoved = inputMoved || (b && b->speed > 0.1F);
        pathSeen = pathSeen || (a && !a->path.empty()) || (b && !b->path.empty());
        if (a && !toiletRequested && gtc::distance(a->position, alpha.world().toiletFront) <= 1.55F) {
            if (!alpha.sendToilet("shit")) return 1;
            toiletRequested = true;
        }
        const gtc::Character* aOther = beta.state().character(alpha.playerId());
        toiletSeen = toiletSeen || (a && aOther && a->toiletAction == gtc::ToiletAction::Shit &&
            aOther->toiletAction == gtc::ToiletAction::Shit && a->onToilet &&
            alpha.state().toiletUser == alpha.playerId() && beta.state().toiletUser == alpha.playerId());
        if (replicated && customized && inputMoved && pathSeen && toiletSeen) break;
    }
    const bool passed = replicated && customized && inputMoved && pathSeen && toiletSeen;
    std::error_code code;
    std::filesystem::create_directories(output, code);
    if (passed) {
        gtc::native::WorldRenderer renderer;
        if (!renderer.load(root, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        struct Capture { const char* name; gtc::native::NativeClient* client; gtc::native::ViewState view; };
        gtc::native::ViewState tps;
        tps.yaw = 0;
        gtc::native::ViewState fps;
        fps.mode = gtc::native::ViewMode::FirstPerson;
        fps.pitch = -1.20;
        gtc::native::ViewState god;
        god.mode = gtc::native::ViewMode::God;
        const Capture captures[] = {{"alpha-tps", &alpha, tps}, {"beta-tps", &beta, tps},
                                    {"alpha-fps", &alpha, fps}, {"god", &alpha, god}};
        for (const Capture& capture : captures) {
            gtc::native::NativeFrame frame;
            if (!renderer.render(capture.client->world(), capture.client->state(), capture.client->playerId(),
                                 capture.view, frame, error) || !frame.save(output + "/" + capture.name + ".png")) {
                std::cerr << "Native capture failed: " << error << '\n';
                return 1;
            }
        }
    }
    std::ofstream report(output + "/report.json");
    report << "{\"passed\":" << (passed ? "true" : "false") << ",\"alpha\":" << alpha.playerId()
           << ",\"beta\":" << beta.playerId() << ",\"tick\":" << alpha.state().tick
           << ",\"replicated\":" << (replicated ? "true" : "false")
           << ",\"customized\":" << (customized ? "true" : "false")
           << ",\"inputMoved\":" << (inputMoved ? "true" : "false")
           << ",\"pathSeen\":" << (pathSeen ? "true" : "false")
           << ",\"toiletSeen\":" << (toiletSeen ? "true" : "false")
           << ",\"iterations\":" << iterations << "}\n";
    std::cout << "native smoke: " << (passed ? "PASS" : "FAIL") << ", alpha=" << alpha.playerId()
              << ", beta=" << beta.playerId() << ", tick=" << alpha.state().tick << '\n';
    if (!passed) std::cerr << "replication=" << replicated << " customize=" << customized
                           << " input=" << inputMoved << " path=" << pathSeen << " toilet=" << toiletSeen << '\n';
    return passed ? 0 : 1;
}
