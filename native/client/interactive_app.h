#include "client/interactive.h"

#include "client/client.h"
#include "client/renderer.h"
#include "client/skeleton_room.h"
#include "client/playground_room.h"
#include "client/skeleton_comparison_ui.h"
#include "client/skeleton_toe_editor_ui.h"
#include "client/skeleton_toe_workspace.h"
#include "client/room_freecam.h"
#include "client/view_controls.h"
#include "client/diagnostics.h"
#include "client/cheat_menu.h"
#include "client/crosshair.h"
#include "client/fps_counter.h"
#include "client/mode_bar.h"
#include "client/interaction_wheel.h"
#include "client/overlay_panel.h"
#include "client/status_panel.h"
#include "client/target_prompt.h"
#include "client/ui_layer.h"
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
#include <utility>
#include <vector>


namespace gtc::native {
namespace interactive_detail {

// The character creator used to live here and opened on C. It is gone: the
// test room took the key, and the next creator will be built fresh rather than
// grown back out of this switchboard.
enum class Overlay { None, Wheel, Cheats, Help };


class App {
public:
    explicit App(InteractiveOptions options) : options_(std::move(options)), toeWorkspace_(options_.root) {}

    int run();


private:
    const Character* local() const { return client_.state().character(client_.playerId()); }

    // The whole interface, in one place, drawn into a frame or described as
    // quads for a device depending on what `frame.ui` says.
    void drawInterface(NativeFrame& frame) {
        drawCrosshair(frame, view_);
        if (overlay_ == Overlay::None && view_.highlightToilet) {
            // Laid out when the window changes size, not every frame: the prompt
            // is two measured lines of type, and measuring them at 300 frames a
            // second is a font query nobody needs.
            if (promptWidth_ != frame.width || promptHeight_ != frame.height) {
                prompt_ = layoutTargetPrompt(frame.width, frame.height, frame.height / 2);
                promptWidth_ = frame.width;
                promptHeight_ = frame.height;
            }
            drawTargetPrompt(frame, prompt_, "Toilet");
        }
        drawOpenOverlay(frame);
        drawModeBar(frame, view_.mode, pointerBlock());
        if (const Character* citizen = local()) {
            const NeedsStatus status{static_cast<int>(std::lround(citizen->needs.bladder)),
                                     static_cast<int>(std::lround(citizen->needs.bowel)),
                                     static_cast<int>(std::lround(citizen->health))};
            const ModeBarLayout bar = layoutModeBar(frame.width, frame.height);
            drawNeedsPanel(frame, layoutNeedsPanel(frame.width, frame.height, bar.top, bar.width), status);
        }
        drawFpsCounter(frame, view_.fps);
    }

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
                drawCheatMenu(frame, bladder, bowel, pointerCheat());
                return;
            }
            case Overlay::Help:
                drawManual(frame);
                return;
        }
    }

    // The one definition of "I am pointing at the throne": the light in the
    // world, the words under the reticle and the E key all ask this, so they
    // cannot disagree. Standing range is not enough -- E fires on the same
    // condition the highlight draws on, which is what the King asked for.
    bool toiletPointed() const {
        const Character* citizen = local();
        if (citizen == nullptr) return false;
        if (distance(citizen->position, client_.world().toiletFront) > client_.world().interactionRange) return false;
        if (client_.state().toiletUser != 0 && client_.state().toiletUser != citizen->id) return false;
        const double x = captured_ ? view_.width * 0.5 : mouseX_;
        const double y = captured_ ? view_.height * 0.5 : mouseY_;
        return toiletHit(x, y);
    }

    void interact() {
        const Character* citizen = local();
        if (!citizen) return;
        // Standing up needs no aim: you are already on the thing you are using,
        // and making people find it with the reticle behind them is punishment.
        if (citizen->toiletAction != ToiletAction::None) {
            client_.sendToilet("stand");
            return;
        }
        if (!toiletPointed()) return;
        openOverlay(Overlay::Wheel);
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
        return pointerOnTarget(view_.width, view_.height, sx, sy, x, y);
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

    void updateHighlight() { view_.highlightToilet = overlay_ == Overlay::None && toiletPointed(); }

    // The pointer, for the pieces that answer to it. When the mouse is captured it
    // is pinned to the middle of the frame, which is where the reticle is.
    std::pair<int, int> pointer() const {
        if (captured_) return {static_cast<int>(view_.width * 0.5), static_cast<int>(view_.height * 0.5)};
        return {static_cast<int>(std::lround(mouseX_)), static_cast<int>(std::lround(mouseY_))};
    }

    int pointerBlock() const {
        if (overlay_ != Overlay::None || captured_) return -1;
        const auto at = pointer();
        return modeBarAt(layoutModeBar(view_.width, view_.height), at.first, at.second);
    }

    int pointerCheat() const {
        if (overlay_ != Overlay::Cheats) return -1;
        const auto at = pointer();
        return cheatMenuAt(layoutCheatMenu(view_.width, view_.height), at.first, at.second);
    }

    void enterRoom() {
        if (!room_.ready()) return;
        endOrbit();
        roomRestoreCapture_ = captured_;
        keys_.clear();
        roomCamera_.clear();
        leftDown_ = rightDown_ = false;
        testRoom_ = true;
        window_.captureMouse(true);
        captured_ = true;
        sendInput();
    }

    void handleRoom(const WindowEvent& event) {
        if (toeEditorUi_.handle(event,captured_,view_.width,view_.height,room_)) {
            if (event.type==WindowEvent::Type::KeyDown && event.key=='t' && toeEditorUi_.open()) {
                roomCamera_.clear(); window_.captureMouse(false); captured_=false;
                const auto selection=toeEditorUi_.selection();
                toeEditorUi_.select(selection.male,false,'L',selection.digit,selection.segment);
                room_.restoreOriginalPose();
            } else if (event.type==WindowEvent::Type::KeyDown && event.key=='t' && !toeEditorUi_.open()) {
                if(!room_.restoreReferencePose())std::cerr << "Unable to apply toe calibration\n";
            }
            return;
        }
        if (toeWorkspace_.handle(event,toeEditorUi_,room_)) return;
        if (roomUi_.handle(event,captured_,view_.width,view_.height,room_)) return;
        const RoomFreecam::Action action = roomCamera_.handle(event, captured_);
        if (action == RoomFreecam::Action::Exit) {
                testRoom_ = false;
            roomCamera_.clear();
            keys_.clear();
            window_.captureMouse(roomRestoreCapture_);
            captured_ = roomRestoreCapture_;
            roomRestoreCapture_ = false;
            sendInput();
        } else if (action == RoomFreecam::Action::Capture) {
            window_.captureMouse(true);
            captured_ = true;
        } else if (action == RoomFreecam::Action::Release) {
                window_.captureMouse(false);
            captured_ = false;
        } else if (action == RoomFreecam::Action::ToggleLines) {
            room_.toggleLineView();
        } else if (action == RoomFreecam::Action::Screenshot) {
            screenshot_ = true;
        } else if (action == RoomFreecam::Action::BendJoints) {
            room_.adjustReferenceGroup(event.key, roomCamera_.reverseBend() ? -5.0 : 5.0,
                                       roomCamera_.fingerJoint());
        } else if (action == RoomFreecam::Action::RestorePose) {
                room_.restoreReferencePose();
        } else if (action == RoomFreecam::Action::RestoreOriginal) {
            room_.restoreOriginalPose();
        }
    }

    void updatePlaygroundDragRay() {
        const Camera camera=playgroundCamera_.camera(view_.width,view_.height,view_.gpu);
        const double x=(roomDragX_-camera.width*0.5)/camera.focal(),y=(camera.height*0.5-roomDragY_)/camera.focal();
        playground_.setDragRay(camera.position,normalized(camera.world({x,y,1.0})-camera.position));
    }
    void stopPlaygroundDrag() { playgroundDragging_=false; playground_.endDrag(); }
    void enterPlayground() {
        if (!playground_.reset()) { std::cerr << "Unable to start playground\n"; return; }
        playgroundReturnToRoom_=testRoom_; roomCamera_.clear(); endOrbit();
        playgroundRestoreCapture_=captured_; keys_.clear(); leftDown_=rightDown_=false;
        testRoom_=false; playgroundMode_=true;
        playgroundCamera_.reset({0,1.4,3.2},0,0);
        window_.captureMouse(true); captured_=true; sendInput();
    }
    void leavePlayground() {
        stopPlaygroundDrag(); playgroundCamera_.clear(); playgroundMode_=false;
        testRoom_=playgroundReturnToRoom_; playgroundReturnToRoom_=false;
        window_.captureMouse(playgroundRestoreCapture_); captured_=playgroundRestoreCapture_;
        playgroundRestoreCapture_=false; keys_.clear(); sendInput();
    }
    void handlePlayground(const WindowEvent& event) {
        if (event.type==WindowEvent::Type::KeyDown && event.key=='p') { if (!event.repeat) leavePlayground(); return; }
        if (playgroundDragging_ && event.type==WindowEvent::Type::MouseUp && event.button==1) { stopPlaygroundDrag(); return; }
        if (playgroundDragging_ && event.type==WindowEvent::Type::MouseMove) {
            roomDragX_=std::clamp(captured_ ? roomDragX_+event.dx : event.x,0.0,double(std::max(1,view_.width)-1));
            roomDragY_=std::clamp(captured_ ? roomDragY_+event.dy : event.y,0.0,double(std::max(1,view_.height)-1));
            updatePlaygroundDragRay(); return;
        }
        if (event.type==WindowEvent::Type::MouseDown && event.button==1) {
            playgroundDragging_=true; roomDragX_=captured_ ? view_.width*0.5 : event.x;
            roomDragY_=captured_ ? view_.height*0.5 : event.y;
            updatePlaygroundDragRay(); playground_.beginDrag(); return;
        }
        if (event.type==WindowEvent::Type::MouseDown && event.button==3 && !captured_) {
            window_.captureMouse(true); captured_=true; return;
        }
        if (event.type==WindowEvent::Type::KeyDown && !event.repeat && event.key=='c') { leavePlayground(); return; }
        if (event.type==WindowEvent::Type::KeyDown && !event.repeat && event.key=='r') { stopPlaygroundDrag(); playground_.reset(); return; }
        if (event.type==WindowEvent::Type::KeyDown && !event.repeat && event.key==kKeyF12) { screenshot_=true; return; }
        if (event.type==WindowEvent::Type::KeyDown && event.key==kKeyEscape) {
            stopPlaygroundDrag(); window_.captureMouse(false); captured_=false; return;
        }
        const auto action=playgroundCamera_.handle(event,captured_);
        if (action==RoomFreecam::Action::Exit) leavePlayground();
        else if (action==RoomFreecam::Action::Capture) { window_.captureMouse(true); captured_=true; }
        else if (action==RoomFreecam::Action::Release) { stopPlaygroundDrag(); window_.captureMouse(false); captured_=false; }
        else if (action==RoomFreecam::Action::Screenshot) screenshot_=true;
        else if (action==RoomFreecam::Action::RestorePose) {
            stopPlaygroundDrag(); if (!playground_.reset()) std::cerr << "Unable to reset playground\n";
        }
    }

    void handle(const WindowEvent& event);


    void cheat(int key);


    bool crosshairTarget(const ViewState& controls, gtc::Vec2& target, float& height) const;


    void sendInput();


    void updateGod(float dt);


    SkeletonRoom room_;
    PlaygroundRoom playground_;
    RoomFreecam roomCamera_,playgroundCamera_;
    bool playgroundMode_=false,playgroundDragging_=false,playgroundReturnToRoom_=false,playgroundRestoreCapture_=false;
    SkeletonComparisonUi roomUi_;
    SkeletonToeEditorUi toeEditorUi_;
    bool testRoom_ = false;
    bool roomRestoreCapture_ = false;
    double roomDragX_ = 0, roomDragY_ = 0;
    ClientState presentationPrevious_, presentationTarget_, presentation_;
    std::uint64_t presentationVersion_ = 0;
    std::chrono::steady_clock::time_point snapshotAt_{};
    double snapshotInterval_ = 0.05;
    bool presentationReady_ = false;
    InteractiveOptions options_;
    SkeletonToeWorkspace toeWorkspace_;
    NativeClient client_;
    WorldRenderer renderer_;
    NativeWindow window_;
    ViewState view_;
    Overlay overlay_ = Overlay::None;
    // Reused every frame: a vector built inside the loop is an allocation per
    // frame for the sake of a handful of events.
    std::vector<WindowEvent> events_;
    UiLayer layer_;
    TargetPromptLayout prompt_{};
    int promptWidth_ = 0, promptHeight_ = 0;
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

}  // namespace interactive_detail
}  // namespace gtc::native
