#include "client/smoke_body.h"
#include "client/smoke_motion.h"
#include "vulkan/vk_renderer.h"
#include "client/smoke_streams.h"
#include "client/blockbody.h"
#include "client/renderer.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <cstdlib>
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
        if (argument == "--vulkan-report") {
            // A backend that cannot start has to say so and return, not take the
            // process with it: this is the machine's answer to "can it draw".
            std::cout << "vulkan: " << (gtc::native::vulkanAvailable() ? "available" : "unavailable") << '\n';
            gtc::native::shutdownVulkan();
            return 0;
        }
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
        !alpha.sendToggleZip() || !alpha.sendAttack(beta.playerId(), true) ||
        !alpha.sendPath(alpha.world().toiletFront) || !beta.sendInput({0, 1}, false, false, 3.14159265F, 0, true)) {
        std::cerr << "Could not send native smoke commands\n";
        return 1;
    }
    bool replicated = false, customized = false, inputMoved = false, pathSeen = false;
    bool backwardReplicated = false, slowReplicated = false;
    bool toiletRequested = false, toiletSeen = false;
    bool zipReplicated = false, attackReplied = false, zipRestoreSent = false, zipRestored = false;
    int iterations = 0;
    for (; iterations < 100; ++iterations) {
        if (!receivePair(alpha, beta, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        if (iterations == 5) {
            if (!beta.sendInput({}, false, false, 0, 0) || !beta.sendPath({3.0F, 2.3F})) return 1;
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
        backwardReplicated = backwardReplicated || (b && b->speed > 0.1F && b->manualMove.y > 0.9F &&
            std::abs(b->yaw - 3.14159265F) < 0.001F);
        slowReplicated = slowReplicated || (b && b->manualMove.y > 0.9F &&
            std::abs(b->speed * 3.6F - 4.0F) < 0.01F);
        pathSeen = pathSeen || (a && !a->path.empty()) || (b && !b->path.empty());
        zipReplicated = zipReplicated || (a && aInB && a->unzipped && aInB->unzipped);
        attackReplied = attackReplied || (a && a->notice.find("Physical shot") != std::string::npos &&
            !alpha.state().projectiles.empty());
        if (zipReplicated && attackReplied && !zipRestoreSent) {
            if (!alpha.sendToggleZip()) return 1;
            zipRestoreSent = true;
        }
        zipRestored = zipRestored || (zipRestoreSent && a && aInB && !a->unzipped && !aInB->unzipped);
        if (a && !toiletRequested && gtc::distance(a->position, alpha.world().toiletFront) <= 1.55F) {
            if (!alpha.sendToilet("shit")) return 1;
            toiletRequested = true;
        }
        const gtc::Character* aOther = beta.state().character(alpha.playerId());
        toiletSeen = toiletSeen || (a && aOther && a->toiletAction == gtc::ToiletAction::Shit &&
            aOther->toiletAction == gtc::ToiletAction::Shit && a->onToilet &&
            alpha.state().toiletUser == alpha.playerId() && beta.state().toiletUser == alpha.playerId());
        if (replicated && customized && inputMoved && backwardReplicated && slowReplicated && pathSeen && toiletSeen && zipRestored) break;
    }
    bool crouchReplicated = false, proneReplicated = false, chargeReplicated = false;
    if (replicated && customized && inputMoved && backwardReplicated && slowReplicated && pathSeen && toiletSeen && zipRestored) {
        for (int i = 0; i < 30; ++i) {
            if (i == 0 && !beta.sendInput({}, false, false, 0, 0, false, true, false)) return 1;
            if (i == 5 && !beta.sendInput({}, false, false, 0, 0, false, true, true)) return 1;
            if (i == 10 && !beta.sendInput({}, false, false, 0, 0, false, false, true)) return 1;
            if (i == 20 && !beta.sendInput({}, false, false, 0, 0)) return 1;
            if (!receivePair(alpha, beta, error)) {
                std::cerr << error << '\n';
                return 1;
            }
            const gtc::Character* remote = alpha.state().character(beta.playerId());
            crouchReplicated = crouchReplicated || (remote && remote->crouched && !remote->prone);
            proneReplicated = proneReplicated || (remote && remote->prone);
            chargeReplicated = chargeReplicated || (remote && remote->charge > 0.02F);
        }
    }
    const bool passed = replicated && customized && inputMoved && backwardReplicated && slowReplicated && pathSeen &&
        toiletSeen && zipRestored && crouchReplicated && proneReplicated && chargeReplicated;
    bool motion = false;
    std::error_code code;
    std::filesystem::create_directories(output, code);
    if (passed) {
        gtc::native::WorldRenderer renderer;
        // The anatomical camera checks below are written against the imported
        // scan's topology, so the harness opts into it; the game draws the
        // authored block body.
        renderer.loadImportedBody(true);
        if (!renderer.load(root, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        motion = motionChecks(renderer, error);
        if (!motion) {
            std::cerr << "Native motion checks failed: " << error << '\n';
            return 1;
        }
        struct Capture { const char* name; gtc::native::NativeClient* client; gtc::native::ViewState view; };
        gtc::native::ViewState tps;
        tps.yaw = 0;
        gtc::native::ViewState fps;
        fps.mode = gtc::native::ViewMode::FirstPerson;
        fps.pitch = -2.0F;
        gtc::native::ViewState fpsLevel;
        fpsLevel.mode = gtc::native::ViewMode::FirstPerson;
        gtc::native::ViewState fpsExtreme = fps;
        if (const gtc::Character* local = alpha.state().character(alpha.playerId())) fpsExtreme.yaw = local->yaw + 3.0F;
        gtc::native::ViewState god;
        god.mode = gtc::native::ViewMode::God;
        const Capture captures[] = {{"alpha-tps", &alpha, tps}, {"beta-tps", &beta, tps},
                                    {"alpha-fps", &alpha, fps}, {"alpha-fps-extreme", &alpha, fpsExtreme},
                                    {"alpha-fps-level", &alpha, fpsLevel}, {"beta-fps-level", &beta, fpsLevel},
                                    {"god", &alpha, god}};
        for (const Capture& capture : captures) {
            gtc::native::NativeFrame frame;
            if (!renderer.render(capture.client->world(), capture.client->state(), capture.client->playerId(),
                                 capture.view, frame, error) || !frame.save(output + "/" + capture.name + ".png")) {
                std::cerr << "Native capture failed: " << error << '\n';
                return 1;
            }
        }
        gtc::native::ClientState standing = alpha.state();
        auto male = std::find_if(standing.characters.begin(), standing.characters.end(), [&](const gtc::Character& character) {
            return character.id == alpha.playerId();
        });
        if (male != standing.characters.end()) {
            male->onToilet = false; male->toiletAction = gtc::ToiletAction::None;
            male->position = {0, 0}; male->yaw = 0;
            const auto seedStream = [&] { gtc::native::seedMeatusStream(standing, *male); };
            gtc::native::ViewState standingFps = fps; standingFps.yaw = male->yaw;
            gtc::native::NativeFrame frame;
            if (!renderer.render(alpha.world(), standing, alpha.playerId(), standingFps, frame, error) ||
                !frame.save(output + "/alpha-standing-fps.png")) {
                std::cerr << "Native standing male capture failed: " << error << '\n';
                return 1;
            }
            male->toiletAction = gtc::ToiletAction::Piss; male->pissing = true; male->pitch = -0.32F;
            seedStream();
            if (!renderer.render(alpha.world(), standing, alpha.playerId(), standingFps, frame, error) ||
                !frame.save(output + "/alpha-piss-fps.png")) {
                std::cerr << "Native standing male piss capture failed: " << error << '\n';
                return 1;
            }
            male->toiletAction = gtc::ToiletAction::None; male->pissing = false;
            const auto stanceCapture = [&](const char* name, const gtc::native::ViewState& stanceView) {
                return renderer.render(alpha.world(), standing, alpha.playerId(), stanceView, frame, error) && frame.save(output + "/" + name + ".png");
            };
            male->unzipped = male->pissing = true; male->pitch = 1.57079633F; seedStream();
            gtc::native::ViewState airView = tps; airView.yaw = 0.85F;
            if (!stanceCapture("alpha-air-piss-tps", airView)) return 1;
            gtc::native::ViewState toiletView = fpsLevel; toiletView.yaw = 3.14159265F; toiletView.pitch = -0.9F;
            if (!gtc::native::orbitKeepsControlsFrozen() ||
                !gtc::native::seedToiletStreamExhibit(standing, alpha.playerId(), root, standing.tick) ||
                !stanceCapture("toilet-water-fps", toiletView)) return 1;
            gtc::native::ViewState npcView = tps; npcView.yaw = -0.65F; npcView.pitch = -0.18F; npcView.distance = 2.4F; npcView.width = 960; npcView.height = 720;
            if (!stanceCapture("npc-piss-tps", npcView)) return 1;
            gtc::native::ViewState toiletNpcView = npcView; toiletNpcView.yaw = 0.9F; toiletNpcView.pitch = -0.22F;
            if (!gtc::native::seedNpcToiletExhibit(standing, root, standing.tick) ||
                !stanceCapture("npc-toilet-tps", toiletNpcView)) return 1;
            for (auto& citizen : standing.characters) citizen.pissing = citizen.unzipped = false;
            standing.projectiles.clear(); standing.waterRipples.clear();
            male->position = {0, 0}; male->yaw = male->pitch = male->pissYaw = male->pissPitch = 0;
            male->crouched = true;
            if (!stanceCapture("alpha-crouch-tps", tps)) return 1;
            male->crouched = false;
            male->charge = 0.65F;
            if (!stanceCapture("alpha-charge-tps", tps)) return 1;
            male->charge = 1.0F;
            if (!stanceCapture("alpha-full-charge-tps", tps)) return 1;
            male->charge = 0;
            male->prone = true;
            gtc::native::ViewState proneFps = fpsLevel; proneFps.yaw = male->yaw;
            if (!stanceCapture("alpha-prone-tps", tps) || !stanceCapture("alpha-prone-fps", proneFps)) return 1;
            male->prone = false;
            male->elevation = 0.5F;
            male->grounded = false;
            if (!stanceCapture("alpha-jump-tps", tps)) return 1;
            for (gtc::Character& other : standing.characters) {
                if (other.id != alpha.playerId()) other.position = {8.0F, 6.0F};
            }
            male->elevation = 0; male->grounded = true;
            male->speed = gtc::kFastWalkSpeed; male->manualMove = {0, 1};
            if (!stanceCapture("alpha-walk-tps", tps)) return 1;
            male->speed = 0; male->manualMove = {}; male->appearance.coat = false;
            gtc::native::ViewState idleView = tps; idleView.yaw = 1.57079633F; idleView.distance = 2.7F;
            if (!stanceCapture("alpha-tent-tps", idleView)) { return 1; } male->appearance.coat = true;
            male->position = {0, -3.25F}; male->yaw = 3.14159265F;
            male->elevation = 0.55F; male->grounded = false;
            male->grabbing = true; male->grabHeight = gtc::kToiletTankHeight;
            male->speed = gtc::kCrouchSpeed;
            male->manualMove = {0, -1};
            gtc::native::ViewState grabView = tps; grabView.yaw = male->yaw + 0.85F;
            if (!stanceCapture("alpha-grab-tps", grabView)) return 1;
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
           << ",\"crouchReplicated\":" << (crouchReplicated ? "true" : "false")
           << ",\"proneReplicated\":" << (proneReplicated ? "true" : "false")
           << ",\"chargeReplicated\":" << (chargeReplicated ? "true" : "false")
           << ",\"motion\":" << (motion ? "true" : "false")
           << ",\"iterations\":" << iterations << "}\n";
    std::cout << "native smoke: " << (passed ? "PASS" : "FAIL") << ", alpha=" << alpha.playerId()
              << ", beta=" << beta.playerId() << ", tick=" << alpha.state().tick << '\n';
    if (!passed) std::cerr << "replication=" << replicated << " customize=" << customized
                           << " input=" << inputMoved << " backward=" << backwardReplicated
                           << " slow=" << slowReplicated << " path=" << pathSeen << " toilet=" << toiletSeen
                           << " crouch=" << crouchReplicated << " prone=" << proneReplicated
                           << " charge=" << chargeReplicated << " zip=" << zipRestored << " attack=" << attackReplied << '\n';
    return passed ? 0 : 1;
}
