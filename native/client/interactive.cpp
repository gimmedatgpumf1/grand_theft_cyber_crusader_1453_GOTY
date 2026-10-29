#include "client/interactive.h"

#include "client/client.h"
#include "client/renderer.h"
#include "client/skeleton_room.h"
#include "client/view_controls.h"
#include "client/diagnostics.h"
#include "client/cheat_menu.h"
#include "client/crosshair.h"
#include "client/fps_counter.h"
#include "client/mode_bar.h"
#include "client/interaction_wheel.h"
#include "client/overlay_panel.h"
#include "client/status_panel.h"
#include "client/window.h"
#include "core/projectile.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <vector>

namespace gtc::native {
namespace {

// The character creator used to live here and opened on C. It is gone: the
// test room took the key, and the next creator will be built fresh rather than
// grown back out of this switchboard.
// A frame rate that only lives in the HUD is a frame rate nobody can quote.
// With GTC_FPS=1 the loop also writes an average to stderr once a second, which
// is the number to send back when asking whether the target was met.
// What the second is made of, not just how many frames fitted in it: a frame
// rate alone cannot say whether the time went into drawing or into getting the
// picture onto the screen, and those two want opposite fixes.
void reportFrameRate(const std::chrono::steady_clock::time_point& now, double renderMs, double presentMs,
                     double logicMs) {
    const char* setting = std::getenv("GTC_FPS");
    if (!setting || !*setting || *setting == '0') return;
    static auto window = now;
    static int frames = 0;
    static double renderTotal = 0.0, presentTotal = 0.0, logicTotal = 0.0;
    ++frames;
    renderTotal += renderMs;
    presentTotal += presentMs;
    logicTotal += logicMs;
    const auto span = std::chrono::duration<double>(now - window).count();
    if (span < 1.0) return;
    // Three numbers, because a frame rate alone cannot say where the millisecond
    // went: drawing wants one fix, getting the picture onto the screen wants
    // another, and the game's own work wants a third.
    std::fprintf(stderr, "[fps] %.1f (%.2f ms/frame: draw %.2f, present %.2f, logic %.2f)\n", frames / span,
                 1000.0 * span / frames, renderTotal / frames, presentTotal / frames, logicTotal / frames);
    window = now;
    frames = 0;
    renderTotal = presentTotal = logicTotal = 0.0;
}

enum class Overlay { None, Wheel, Cheats, Help };

class App {
public:
    explicit App(InteractiveOptions options) : options_(std::move(options)) {}

    int run() {
        std::string error;
        if (!client_.connect(options_.host, options_.port, error) || !client_.sendJoin(options_.name)) {
            std::cerr << error << '\n';
            return 1;
        }
        if (!renderer_.load(options_.root, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        std::string roomError;
        if (!room_.load(options_.root, roomError)) std::cerr << "Test room unavailable: " << roomError << '\n';
        if (!window_.open("GRAND THEFT CYBER CRUSADER 1453 - Native", error)) {
            std::cerr << error << '\n';
            return 1;
        }
        window_.size(view_.width, view_.height);
        view_.gpu = window_.gpu();
        // Which backend is actually carrying the frame is the first thing worth
        // knowing when a frame is slow; guessing it wastes an evening.
        tracef("window: backend=%s size=%dx%d", window_.backend(), view_.width, view_.height);
        std::cout << "backend: " << window_.backend() << " at " << view_.width << 'x' << view_.height << '\n';
        mouseX_ = view_.width * 0.5;
        mouseY_ = view_.height * 0.5;
        view_.yaw = 3.14159265F;
        view_.pitch = -0.18F;
        using Clock = std::chrono::steady_clock;
        auto previous = Clock::now();
        auto inputTime = previous;
        NativeFrame frame;
        bool running = true;
        // The two halves of a frame, measured where the test room runs.
        double renderMilliseconds = 0.0;
        double presentMilliseconds = 0.0;
        double logicMilliseconds = 0.0;
        while (running && window_.opened() && client_.connected()) {
            const auto now = Clock::now();
            const float elapsed = std::max(0.0001F, std::chrono::duration<float>(now - previous).count());
            const float dt = std::min(0.05F, elapsed);
            view_.fps = view_.fps > 0 ? view_.fps * 0.90 + (1.0 / elapsed) * 0.10 : 1.0 / elapsed;
            previous = now;
            const auto loopStart = Clock::now();
            reportFrameRate(now, renderMilliseconds, presentMilliseconds, logicMilliseconds);
            if (!client_.receive(error, 0)) {
                std::cerr << error << '\n';
                return 1;
            }
            trackBodyTurn();
            std::vector<WindowEvent> events;
            if (!window_.poll(events)) break;
            window_.size(view_.width, view_.height);
            for (const WindowEvent& event : events) {
                if (event.type == WindowEvent::Type::Close) running = false;
                else handle(event);
            }
            updateGod(dt);
            if (now - inputTime >= std::chrono::milliseconds(50)) {
                sendInput();
                inputTime = now;
            }
            updatePresentation();
            updateHighlight();
            updateReticle();
            const auto renderStart = Clock::now();
            // Everything above this line is the game's own work: reading the
            // server, the events, the state the frame is drawn from.
            logicMilliseconds = std::chrono::duration<double, std::milli>(renderStart - loopStart).count();
            // The two halves of the frame, kept in the loop's scope so the
            // reporter outside it sees this iteration's numbers rather than the
            // zeros it was initialised with.
            renderMilliseconds = 0.0;
            presentMilliseconds = 0.0;
            if (testRoom_ && room_.ready()) {
                const double seconds = std::chrono::duration<double>(now.time_since_epoch()).count();
                // The room is the one path with no overlay drawn over it on the
                // CPU, so its frame can go straight to the window.
                frame.wantPixels = false;
                room_.render(frame, view_, seconds);
                renderMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - renderStart).count();
                view_.frameMilliseconds = view_.frameMilliseconds > 0
                    ? view_.frameMilliseconds * 0.9 + renderMilliseconds * 0.1
                    : renderMilliseconds;
                const auto presentStart = Clock::now();
                if (!window_.present(frame)) {
                    std::cerr << "Native window presentation failed\n";
                    return 1;
                }
                presentMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - presentStart).count();
                view_.drawMilliseconds = renderMilliseconds;
                view_.presentMilliseconds = presentMilliseconds;
                view_.logicMilliseconds = logicMilliseconds;
                continue;
            }
            // The overlay is drawn into this frame on the CPU a few lines down,
            // so this one has to come back through host memory -- but in the
            // order the window wants it. Asking for rgb meant a channel shuffle
            // out of the device and a second one back into the window: two
            // passes over every pixel of every frame, to arrive where the
            // device's own bytes already were.
            frame.wantPixels = true;
            frame.wantsBgra = true;
            if (!renderer_.render(client_.world(), visualState(), client_.playerId(), view_, frame, error)) {
                // A frame that will not draw is the end of the run. Falling back
                // to the software rasterizer hid a broken backend behind a slow
                // one; the failure is the thing worth seeing.
                std::fflush(stdout);
                std::cerr << "frame failed: " << error << '\n';
                std::abort();
            }
            const double renderMs = std::chrono::duration<double, std::milli>(Clock::now() - renderStart).count();
            view_.frameMilliseconds = view_.frameMilliseconds > 0 ? view_.frameMilliseconds * 0.9 + renderMs * 0.1 : renderMs;
            // The only thing drawn over the picture: the frame rate, on the host,
            // into pixels that already exist. Costs a few hundred fills a frame.
            const auto overlayStart = Clock::now();
            drawCrosshair(frame, view_);
            drawOpenOverlay(frame);
            drawModeBar(frame, view_.mode);
            if (const Character* citizen = local()) {
                const NeedsStatus status{static_cast<int>(std::lround(citizen->needs.bladder)),
                                         static_cast<int>(std::lround(citizen->needs.bowel)),
                                         static_cast<int>(std::lround(citizen->health))};
                const ModeBarLayout bar = layoutModeBar(frame.width, frame.height);
                drawNeedsPanel(frame, layoutNeedsPanel(frame.width, frame.height, bar.top, bar.width), status);
            }
            drawFpsCounter(frame, view_.fps);
            renderMilliseconds += std::chrono::duration<double, std::milli>(Clock::now() - overlayStart).count();
            view_.drawMilliseconds = renderMilliseconds;
            view_.logicMilliseconds = logicMilliseconds;
            const auto presentStart = Clock::now();
            if (!window_.present(frame)) {
                std::cerr << "Native window presentation failed\n";
                return 1;
            }
            presentMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - presentStart).count();
            view_.presentMilliseconds = presentMilliseconds;
            if (screenshot_) {
                std::filesystem::create_directories("build/native-client");
                if (!window_.save(frame, "build/native-client/window.png")) {
                    std::cerr << "Native screenshot capture failed\n";
                    return 1;
                }
                screenshot_ = false;
            }
        }
        return 0;
    }

private:
    const Character* local() const { return client_.state().character(client_.playerId()); }

    const ClientState& visualState() const {
        return presentationReady_ ? presentation_ : client_.state();
    }

    void trackBodyTurn() {
        const Character* citizen = local();
        if (!citizen) {
            bodyTrackReady_ = false;
            return;
        }
        if (!bodyTrackReady_ || trackedPlayerId_ != citizen->id) {
            trackedPlayerId_ = citizen->id;
            trackedBodyYaw_ = citizen->yaw;
            trackedToiletAction_ = citizen->toiletAction;
            bodyTrackReady_ = true;
            return;
        }
        const bool wasActing = trackedToiletAction_ != ToiletAction::None;
        const bool isActing = citizen->toiletAction != ToiletAction::None;
        const double cameraDelta = std::atan2(std::sin(citizen->yaw - view_.yaw),
                                              std::cos(citizen->yaw - view_.yaw));
        // A movement exit can rotate the body directly toward the current
        // camera in the same server input. Do not apply that turn twice.
        const bool alreadyCameraAligned = wasActing && !isActing && std::abs(cameraDelta) < 1e-4;
        if (view_.mode == ViewMode::FirstPerson && (wasActing || isActing) && !alreadyCameraAligned) {
            turnViewWithBody(view_, trackedBodyYaw_, citizen->yaw);
            clampViewToBody(view_, citizen->yaw, true);
        }
        trackedBodyYaw_ = citizen->yaw;
        trackedToiletAction_ = citizen->toiletAction;
    }

    void updatePresentation() {
        const auto now = std::chrono::steady_clock::now();
        const ClientState& latest = client_.state();
        if (!presentationReady_) {
            presentationPrevious_ = presentationTarget_ = presentation_ = latest;
            presentationVersion_ = client_.stateVersion();
            snapshotAt_ = now;
            presentationReady_ = true;
            return;
        }
        if (presentationVersion_ != client_.stateVersion()) {
            const double measured = std::chrono::duration<double>(now - snapshotAt_).count();
            if (measured > 0.005) snapshotInterval_ = std::clamp(snapshotInterval_ * 0.75 + measured * 0.25, 0.025, 0.15);
            presentationPrevious_ = presentationTarget_;
            presentationTarget_ = presentation_ = latest;
            presentationVersion_ = client_.stateVersion();
            snapshotAt_ = now;
        }
        const double partialTick = std::chrono::duration<double>(now - snapshotAt_).count() / snapshotInterval_;
        interpolateState(presentationPrevious_, presentationTarget_, presentation_, partialTick);
    }

    ViewState controlView() const { return orbit_.controls(view_); }

    void beginOrbit() {
        if (overlay_ != Overlay::None) return;
        if (orbit_.begin(view_, captured_)) { window_.captureMouse(true); captured_ = true; }
    }

    void endOrbit() {
        if (orbit_.end(view_) && captured_) { window_.captureMouse(false); captured_ = false; }
    }

    void setMode(ViewMode mode) {
        if (view_.mode == mode) return;
        const ViewMode previous = view_.mode;
        view_.mode = mode;
        if (previous == ViewMode::God || mode == ViewMode::God) keys_.clear();
        endOrbit();
        leftDown_ = rightDown_ = false;
        lastLeftClick_ = lastRightClick_ = {};
        view_.aiming = false;
        if (mode == ViewMode::God && captured_) {
            window_.captureMouse(false);
            captured_ = false;
        }
        if (mode == ViewMode::FirstPerson) {
            if (const Character* citizen = visualState().character(client_.playerId())) {
                clampViewToBody(view_, citizen->yaw, true);
            }
        }
        if (mode == ViewMode::ThirdPerson) view_.pitch = std::clamp(view_.pitch, -1.05F, -0.06F);
        if (mode == ViewMode::God) {
            if (const Character* citizen = local()) {
                view_.godX = citizen->position.x;
                view_.godZ = citizen->position.y;
            }
        }
        sendInput();
    }

    void closeOverlay() {
        if (overlay_ == Overlay::None) return;
        overlay_ = Overlay::None;
        const bool recapture = restoreMouseCapture_ && view_.mode == ViewMode::FirstPerson;
        restoreMouseCapture_ = false;
        if (recapture) {
            window_.captureMouse(true);
            captured_ = true;
        }
    }

    void openOverlay(Overlay overlay) {
        if (overlay_ == overlay) {
            closeOverlay();
            return;
        }
        if (overlay_ == Overlay::None) restoreMouseCapture_ = captured_ && view_.mode == ViewMode::FirstPerson;
        overlay_ = overlay;
        endOrbit();
        keys_.clear();
        window_.captureMouse(false);
        captured_ = false;
    }

    // Every open overlay draws something. The switch below has no `default`, so
    // adding a case to the enum and forgetting it here is a compiler warning and
    // not a screen that has gone deaf for no visible reason -- which is exactly
    // what happened when the old panels were deleted and these three states were
    // left behind.
    void drawOpenOverlay(NativeFrame& frame) {
        switch (overlay_) {
            case Overlay::None:
                return;
            case Overlay::Wheel: {
                // The pointer is the mouse: while the circle is open the mouse is
                // not captured, so these are real window pixels and the sector
                // under them is the sector a click would take.
                const InteractionWheelLayout layout = layoutInteractionWheel(frame.width, frame.height);
                drawInteractionWheel(frame, layout, static_cast<int>(std::lround(mouseX_)),
                                     static_cast<int>(std::lround(mouseY_)));
                return;
            }
            case Overlay::Cheats: {
                const Character* citizen = local();
                const int bladder = citizen ? static_cast<int>(std::lround(citizen->needs.bladder)) : 0;
                const int bowel = citizen ? static_cast<int>(std::lround(citizen->needs.bowel)) : 0;
                drawCheatMenu(frame, bladder, bowel);
                return;
            }
            case Overlay::Help:
                drawManual(frame);
                return;
        }
    }

    void interact() {
        const Character* citizen = local();
        if (!citizen) return;
        if (citizen->toiletAction != ToiletAction::None) {
            client_.sendToilet("stand");
        } else if (distance(citizen->position, client_.world().toiletFront) <= client_.world().interactionRange &&
                   (client_.state().toiletUser == 0 || client_.state().toiletUser == citizen->id)) {
            openOverlay(Overlay::Wheel);
        }
    }

    void toiletChoice(int choice) {
        if (choice == 1) client_.sendToilet("sit");
        if (choice == 2) client_.sendToilet("shit");
        if (choice == 3) client_.sendToilet("piss");
        closeOverlay();
    }

    bool ground(double x, double y, gtc::Vec2& point) const {
        const Camera camera = renderer_.viewCamera(client_.world(), visualState(), client_.playerId(), view_);
        const Vec3 forward = normalized(camera.target - camera.position);
        const Vec3 right = normalized(cross(forward, camera.up));
        const Vec3 vertical = normalized(cross(right, forward));
        const Vec3 direction = normalized(forward + right * ((x - view_.width * 0.5) / camera.focal()) +
                                          vertical * ((view_.height * 0.5 - y) / camera.focal()));
        if (direction.y >= -1e-6) return false;
        const Vec3 hit = camera.position + direction * (-camera.position.y / direction.y);
        point = {static_cast<float>(std::clamp(hit.x, -8.7, 8.7)),
                 static_cast<float>(std::clamp(hit.z, -6.7, 6.7))};
        return true;
    }

    bool toiletHit(double x, double y) const {
        const Camera camera = renderer_.viewCamera(client_.world(), visualState(), client_.playerId(), view_);
        const Vec3 point = camera.view({client_.world().toilet.x, 0.72, client_.world().toilet.y});
        if (point.z <= 0.05) return false;
        const double sx = view_.width * 0.5 + camera.focal() * point.x / point.z;
        const double sy = view_.height * 0.5 - camera.focal() * point.y / point.z;
        const double scale = std::min(view_.width / 640.0, view_.height / 480.0);
        return std::hypot(sx - x, sy - y) <= 75 * scale;
    }

    std::uint32_t characterAt(double x, double y) const {
        const Camera camera = renderer_.viewCamera(client_.world(), visualState(), client_.playerId(), view_);
        std::uint32_t best = 0;
        double bestDistance = 46.0;
        for (const Character& character : visualState().characters) {
            if (character.id == client_.playerId()) continue;
            const Vec3 point = camera.view({character.position.x, character.elevation + 0.85, character.position.y});
            if (point.z <= 0.05) continue;
            const double sx = view_.width * 0.5 + camera.focal() * point.x / point.z;
            const double sy = view_.height * 0.5 - camera.focal() * point.y / point.z;
            const double candidate = std::hypot(sx - x, sy - y);
            if (candidate < bestDistance) { bestDistance = candidate; best = character.id; }
        }
        return best;
    }

    bool doubleClick(bool right, double x, double y) {
        const auto now = std::chrono::steady_clock::now();
        auto& time = right ? lastRightClick_ : lastLeftClick_;
        auto& oldX = right ? lastRightX_ : lastLeftX_;
        auto& oldY = right ? lastRightY_ : lastLeftY_;
        const bool result = time.time_since_epoch().count() != 0 && now - time < std::chrono::milliseconds(450) &&
            std::hypot(x - oldX, y - oldY) < 22.0;
        time = now; oldX = x; oldY = y;
        return result;
    }

    void updateReticle() {
        view_.orbitPreview = false;
        if (!orbit_.active) return;
        const ViewState control = controlView();
        const Camera camera = renderer_.viewCamera(client_.world(), visualState(), client_.playerId(), view_);
        gtc::Vec2 target{};
        float height = 0.0F;
        Vec3 world;
        const bool ranged = local() && local()->appearance.male && local()->unzipped;
        if (ranged && crosshairTarget(control, target, height)) {
            world = {target.x, height, target.y};
        } else {
            const Camera frozen = renderer_.viewCamera(client_.world(), visualState(), client_.playerId(), control);
            world = frozen.position + normalized(frozen.target - frozen.position) * 6.0;
        }
        const Vec3 point = camera.view(world);
        if (point.z <= 0.05) return;
        view_.orbitPreview = true;
        view_.reticleX = static_cast<float>(view_.width * 0.5 + camera.focal() * point.x / point.z);
        view_.reticleY = static_cast<float>(view_.height * 0.5 - camera.focal() * point.y / point.z);
    }

    void updateHighlight() {
        const Character* citizen = local();
        view_.highlightToilet = citizen &&
            distance(citizen->position, client_.world().toiletFront) <= client_.world().interactionRange &&
            toiletHit(captured_ ? view_.width * 0.5 : mouseX_, captured_ ? view_.height * 0.5 : mouseY_);
    }

    void handle(const WindowEvent& event) {
        if (event.type == WindowEvent::Type::FocusLoss) {
            keys_.clear();
            endOrbit();
            leftDown_ = rightDown_ = false;
            window_.captureMouse(false);
            captured_ = false;
            restoreMouseCapture_ = false;
            return;
        }
        if (event.type == WindowEvent::Type::MouseMove) {
            mouseX_ = event.x;
            mouseY_ = event.y;
            if (orbit_.active) {
                rotateView(view_, event.dx, event.dy);
            } else if (captured_) {
                rotateView(view_, event.dx, event.dy);
                if (view_.mode == ViewMode::FirstPerson) {
                    if (const Character* citizen = local()) clampViewToBody(view_, citizen->yaw);
                }
            }
            return;
        }
        if (event.type == WindowEvent::Type::Wheel) {
            if (orbit_.active) return; // Zoom is part of the restored camera.
            if (view_.mode == ViewMode::God) view_.godDistance = static_cast<float>(std::clamp(view_.godDistance - event.wheel * 0.9, 8.0, 25.0));
            else view_.distance = static_cast<float>(std::clamp(view_.distance - event.wheel * 0.35, 2.4, 8.0));
            return;
        }
        if (event.type == WindowEvent::Type::MouseDown) {
            mouseX_ = event.x;
            mouseY_ = event.y;
            if (event.button == 2) {
                beginOrbit();
                return;
            }
            if (event.button != 1 && event.button != 3) return;
            if (overlay_ == Overlay::Wheel) {
                // The same layout that drew the wheel answers the click: a hit on
                // an option takes it, a hit in the middle cancels, and a click on
                // the panel around them does nothing, so a stray click cannot
                // choose something by accident.
                const InteractionWheelLayout layout = layoutInteractionWheel(view_.width, view_.height);
                const int hit = interactionWheelHit(layout, static_cast<int>(std::lround(event.x)),
                                                    static_cast<int>(std::lround(event.y)));
                if (event.button != 1) return;
                if (hit >= 0) toiletChoice(hit + 1);
                else if (hit == kWheelCancel) closeOverlay();
                return;
            }
            if (overlay_ != Overlay::None) return;
            const bool doubled = doubleClick(event.button == 3, event.x, event.y);
            if (view_.mode == ViewMode::God) {
                if (event.button == 3) {
                    gtc::Vec2 point;
                    if (ground(event.x, event.y, point)) {
                        client_.sendPath(point, keys_.count(kKeyShift) != 0, doubled);
                    }
                } else if (const std::uint32_t target = characterAt(event.x, event.y)) {
                    client_.sendAttack(target, doubled);
                } else if (local() && local()->appearance.male && local()->unzipped && toiletHit(event.x, event.y)) {
                    client_.sendAttackAt(client_.world().toilet + gtc::Vec2{0.0F, kToiletBowlOffset}, kToiletWaterHeight);
                }
                return;
            }
            if (event.button == 3) {
                rightDown_ = true;
                if (!captured_) { window_.captureMouse(true); captured_ = true; }
                sendInput();
                return;
            }
            if (!captured_) {
                window_.captureMouse(true);
                captured_ = true;
            } else if (local() && local()->appearance.male && local()->unzipped) {
                leftDown_ = true;
                sendInput();
            } else if (toiletHit(view_.width * 0.5, view_.height * 0.5)) {
                interact();
            }
            return;
        }
        if (event.type == WindowEvent::Type::MouseUp) {
            if (event.button == 1) leftDown_ = false;
            if (event.button == 2) endOrbit();
            if (event.button == 3) rightDown_ = false;
            sendInput();
            return;
        }
        if (event.type == WindowEvent::Type::KeyUp) {
            keys_.erase(event.key);
            if (event.key == ' ' || event.key == kKeyLeftControl || event.key == kKeyAlt) sendInput();
            return;
        }
        if (event.type != WindowEvent::Type::KeyDown || event.key == 0) return;
        if (event.key == kKeyF12) {
            if (!event.repeat) screenshot_ = true;
            return;
        }
        if (event.repeat && (event.key == kKeyEscape || event.key == kKeyF1 || event.key == '1' ||
            event.key == '2' || event.key == '3' || event.key == 'e' || event.key == 'c' || event.key == 'h')) return;
        if (overlay_ == Overlay::Wheel) {
            if (event.key >= '1' && event.key <= '3') toiletChoice(event.key - '0');
            else if (event.key == 'e' || event.key == kKeyEscape) closeOverlay();
            return;
        }
        if (overlay_ == Overlay::Cheats) {
            if (!event.repeat) cheat(event.key);
            return;
        }
        if (overlay_ == Overlay::Help) {
            if (event.key == 'h' || event.key == kKeyEscape) closeOverlay();
            return;
        }
        if (event.key == kKeyEscape) {
            keys_.clear();
            endOrbit();
            leftDown_ = rightDown_ = false;
            window_.captureMouse(false);
            captured_ = false;
        } else if (event.key == '1') setMode(ViewMode::ThirdPerson);
        else if (event.key == '2') setMode(ViewMode::God);
        else if (event.key == '3') setMode(ViewMode::FirstPerson);
        else if (event.key == kKeyCapsLock) { if (!event.repeat) slowWalk_ = !slowWalk_; }
        else if (event.key == 't') { if (!event.repeat) client_.sendToggleZip(); }
        else if (event.key == 'e') interact();
        else if (event.key == kKeyF1) openOverlay(Overlay::Cheats);
        else if (event.key == 'c') { if (!event.repeat) testRoom_ = !testRoom_; }
        else if (event.key == 'h') openOverlay(Overlay::Help);
        else {
            keys_.insert(event.key);
            if (!event.repeat && (event.key == ' ' || event.key == kKeyLeftControl || event.key == kKeyAlt)) sendInput();
        }
    }

    void cheat(int key) {
        if (key == kKeyF1 || key == kKeyEscape) {
            closeOverlay();
            return;
        }
        const char* need = key >= '5' ? "bowel" : "bladder";
        float delta = 0;
        if (key == '1' || key == '5') delta = 25;
        if (key == '2' || key == '6') delta = -25;
        if (key == '3' || key == '7') delta = -100;
        if (key == '4' || key == '8') delta = 100;
        if (delta != 0) client_.sendCheat(need, delta);
    }

    bool crosshairTarget(const ViewState& controls, gtc::Vec2& target, float& height) const {
        const Camera camera = renderer_.viewCamera(client_.world(), visualState(), client_.playerId(), controls);
        const Vec3 direction = normalized(camera.target - camera.position);
        const projectile_detail::Point from{static_cast<float>(camera.position.x), static_cast<float>(camera.position.y),
                                            static_cast<float>(camera.position.z)};
        const projectile_detail::Point to = from + projectile_detail::Point{
            static_cast<float>(direction.x * 24.0), static_cast<float>(direction.y * 24.0),
            static_cast<float>(direction.z * 24.0)};
        const auto surface = projectile_detail::sweepSurfaces(from, to, 0.0F, client_.world().obstacles);
        float amount = surface.hit ? surface.amount : 1.0F;
        bool found = surface.hit;
        for (const Character& character : visualState().characters) {
            if (character.id == client_.playerId()) continue;
            float candidate = 1.0F;
            if (projectile_detail::sweepCharacter(from, to, 0.0F, character, candidate) &&
                (!found || candidate < amount)) {
                amount = candidate;
                found = true;
            }
        }
        if (!found) amount = 1.0F / 24.0F; // No surface: converge one metre ahead.
        const projectile_detail::Point at = from + (to - from) * amount;
        target = {at.x, at.z};
        height = at.y;
        return true;
    }

    void sendInput() {
        gtc::Vec2 move{};
        bool sprint = false, piss = false, crouch = false, grab = false, jump = false, block = false;
        const bool ranged = local() && local()->appearance.male && local()->unzipped;
        const ViewState control = controlView();
        float yaw = local() ? local()->yaw : control.yaw;
        view_.aiming = false;
        crouch = overlay_ == Overlay::None && keys_.count(kKeyAlt) != 0;
        if (overlay_ == Overlay::None && view_.mode != ViewMode::God) {
            const double forward = (keys_.count('w') ? 1 : 0) - (keys_.count('s') ? 1 : 0);
            const double right = (keys_.count('d') ? 1 : 0) - (keys_.count('a') ? 1 : 0);
            move = {static_cast<float>(std::sin(control.yaw) * forward - std::cos(control.yaw) * right),
                    static_cast<float>(std::cos(control.yaw) * forward + std::sin(control.yaw) * right)};
            sprint = keys_.count(kKeyShift) != 0;
            piss = ranged && (leftDown_ || (view_.mode == ViewMode::FirstPerson && keys_.count('f') != 0));
            block = rightDown_ && !ranged;
            view_.aiming = rightDown_ && ranged;
            grab = keys_.count(kKeyLeftControl) != 0;
            jump = keys_.count(' ') != 0;
            if (view_.mode == ViewMode::FirstPerson || forward != 0 || right != 0 || grab || ranged) yaw = control.yaw;
        }
        view_.slowWalk = slowWalk_;
        gtc::Vec2 aimTarget{};
        float aimHeight = 0.0F;
        const bool hasAimTarget = ranged && view_.mode != ViewMode::God && crosshairTarget(control, aimTarget, aimHeight);
        client_.sendInput(move, sprint, piss, yaw, control.pitch, slowWalk_, crouch, grab, jump, block,
                          aimTarget, aimHeight, hasAimTarget);
    }

    void updateGod(float dt) {
        if (overlay_ != Overlay::None || view_.mode != ViewMode::God) return;
        const double forward = (keys_.count('w') ? 1 : 0) - (keys_.count('s') ? 1 : 0);
        const double right = (keys_.count('d') ? 1 : 0) - (keys_.count('a') ? 1 : 0);
        const double speed = dt * view_.godDistance * 1.05 * (keys_.count(kKeyShift) ? 1.6 : 1.0);
        view_.godX += static_cast<float>((-std::sin(view_.godYaw) * forward + std::cos(view_.godYaw) * right) * speed);
        view_.godZ += static_cast<float>((-std::cos(view_.godYaw) * forward - std::sin(view_.godYaw) * right) * speed);
        view_.godX = std::clamp(view_.godX, -9.0F, 9.0F);
        view_.godZ = std::clamp(view_.godZ, -7.0F, 7.0F);
    }

    SkeletonRoom room_;
    bool testRoom_ = false;
    ClientState presentationPrevious_, presentationTarget_, presentation_;
    std::uint64_t presentationVersion_ = 0;
    std::chrono::steady_clock::time_point snapshotAt_{};
    double snapshotInterval_ = 0.05;
    bool presentationReady_ = false;
    InteractiveOptions options_;
    NativeClient client_;
    WorldRenderer renderer_;
    NativeWindow window_;
    ViewState view_;
    Overlay overlay_ = Overlay::None;
    std::set<int> keys_;
    bool captured_ = false;
    bool restoreMouseCapture_ = false;
    bool leftDown_ = false, rightDown_ = false;
    OrbitPreview orbit_;
    std::chrono::steady_clock::time_point lastLeftClick_{}, lastRightClick_{};
    double lastLeftX_ = 0, lastLeftY_ = 0, lastRightX_ = 0, lastRightY_ = 0;
    bool bodyTrackReady_ = false;
    std::uint32_t trackedPlayerId_ = 0;
    float trackedBodyYaw_ = 0;
    ToiletAction trackedToiletAction_ = ToiletAction::None;
    bool slowWalk_ = false;
    bool screenshot_ = false;
    double mouseX_ = kDefaultWindowWidth * 0.5, mouseY_ = kDefaultWindowHeight * 0.5;
};

} // namespace

int runInteractive(const InteractiveOptions& options) {
    App app(options);
    return app.run();
}

} // namespace gtc::native
