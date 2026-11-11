// The two long pieces of the interactive client: one frame of the application
// loop, which decides what the client does this frame, and the event handler,
// which decides what a key or a click means in the mode the client is in.

#include "client/interactive_app.h"

namespace gtc::native {
namespace interactive_detail {

int App::run() {
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
        else {toeWorkspace_.syncWhiteAxes(room_);room_.restoreReferencePose();}
        if (!window_.open("GRAND THEFT CYBER CRUSADER 1453 - Native", error)) {
            std::cerr << error << '\n';
            return 1;
        }
        window_.size(view_.width, view_.height);
        view_.gpu = window_.gpu();
        if (std::string(window_.backend()) != "vulkan") {
            std::cerr << "The interactive client requires Vulkan presentation\n";
            return 1;
        }
        // Which backend is actually carrying the frame is the first thing worth
        // knowing when a frame is slow; guessing it wastes an evening.
        tracef("window: backend=%s size=%dx%d", window_.backend(), view_.width, view_.height);
        mouseX_ = view_.width * 0.5;
        mouseY_ = view_.height * 0.5;
        view_.yaw = 3.14159265F;
        view_.pitch = -0.18F;
        using Clock = std::chrono::steady_clock;
        auto previous = Clock::now();
        auto inputTime = previous;
        NativeFrame frame;
        bool running = true;
        while (running && window_.opened() && client_.connected()) {
            const auto now = Clock::now();
            const float elapsed = std::max(0.0001F, std::chrono::duration<float>(now - previous).count());
            const float dt = std::min(0.05F, elapsed);
            view_.fps = view_.fps > 0 ? view_.fps * 0.90 + (1.0 / elapsed) * 0.10 : 1.0 / elapsed;
            previous = now;
            if (!client_.receive(error, 0)) {
                std::cerr << error << '\n';
                return 1;
            }
            if (testRoom_ || playgroundMode_) bodyTrackReady_ = false;
            else trackBodyTurn();
            events_.clear();
            if (!window_.poll(events_)) break;
            window_.size(view_.width, view_.height);
            for (const WindowEvent& event : events_) {
                if (event.type == WindowEvent::Type::Close) running = false;
            else handle(event);
            }
            if (playgroundMode_) {
                if (playgroundDragging_) updatePlaygroundDragRay();
                playground_.advance(dt);
            } else if (testRoom_) {
                roomCamera_.move(dt);
            } else updateGod(dt);
            if (now - inputTime >= std::chrono::milliseconds(50)) {
            sendInput();
                inputTime = now;
            }
            updatePresentation();
            updateHighlight();
            updateReticle();
            if (playgroundMode_ || (testRoom_ && room_.ready())) {
                const double seconds = std::chrono::duration<double>(now.time_since_epoch()).count();
                // Room controls are GPU batches, just like the normal HUD.
                frame.width=std::max(1,view_.width); frame.height=std::max(1,view_.height);
                if (!playgroundMode_ && toeEditorUi_.open() && !toeWorkspace_.prepare(frame,room_,toeEditorUi_)) {
                    std::cerr << "Unable to prepare toe editor\n"; return 1;
                }
                layer_.clear(frame.width,frame.height); frame.ui=&layer_;
                frame.uiScale=1.0; frame.uiOffsetX=frame.uiOffsetY=0.0;
                if (playgroundMode_) { drawFpsCounter(frame,view_.fps); }
                else if (toeEditorUi_.open()) { toeWorkspace_.drawUi(frame,toeEditorUi_); toeEditorUi_.draw(frame,room_); }
                else { roomUi_.draw(frame,room_); drawFpsCounter(frame,view_.fps); }
                if (captured_) {
                    const bool dragging=playgroundMode_ && playgroundDragging_;
                    const int x=int(std::lround(dragging ? roomDragX_ : view_.width*0.5));
                    const int y=int(std::lround(dragging ? roomDragY_ : view_.height*0.5));
                    const Color ink=colorHex("#d7ff54");
                    frame.rectangle(x-5,y,11,1,ink); frame.rectangle(x,y-5,1,11,ink);
                }
                frame.ui=nullptr; frame.uiLayer=&layer_; frame.interfaceInFrame=false;
                frame.wantPixels = screenshot_;
                if (playgroundMode_) {
                    if (!playground_.render(frame,view_,playgroundCamera_)) { std::cerr << "Unable to draw playground\n"; return 1; }
                    frame.logicalUi();
                } else if (toeEditorUi_.open()) {
                    if (!toeWorkspace_.render(frame)) {
                        std::cerr << "Unable to draw toe editor\n"; return 1;
                    }
                } else room_.render(frame, view_, seconds, &roomCamera_);
                frame.uiLayer=nullptr;
                if (!frame.interfaceInFrame || (!frame.presented && !screenshot_)) {
                    std::cerr << "Vulkan did not present the room interface on the device\n";
                    return 1;
                }
                if (!window_.present(frame)) {
                    std::cerr << "Native window presentation failed\n";
                    return 1;
                }
                if (screenshot_) {
                    std::filesystem::create_directories("build/native-client");
                    if (!window_.save(frame, "build/native-client/window.png")) {
                        std::cerr << "Native screenshot capture failed\n";
                        return 1;
                    }
                    screenshot_ = false;
                }
                continue;
            }
            // The UI is geometry submitted with the world, never a CPU overlay.
            frame.width = std::max(1, view_.width);
            frame.height = std::max(1, view_.height);
            layer_.clear(frame.width, frame.height);
            frame.ui = &layer_;
            drawInterface(frame);
            frame.ui = nullptr;
            frame.uiLayer = &layer_;
            frame.wantPixels = false;
            frame.wantsBgra = false;
            frame.interfaceInFrame = false;
            // A screenshot is the one thing a presented frame cannot serve: it
            // needs the pixels, so this frame comes back to the host on purpose.
            if (screenshot_) {
                frame.wantPixels = true;
                frame.wantsBgra = false;
            }
            if (!renderer_.render(client_.world(), visualState(), client_.playerId(), view_, frame, error)) {
                // A frame that will not draw is the end of the run. Falling back
                // to the software rasterizer hid a broken backend behind a slow
                // one; the failure is the thing worth seeing.
                std::fflush(stdout);
                std::cerr << "frame failed: " << error << '\n';
                std::abort();
            }
            frame.uiLayer = nullptr;
            if (!frame.interfaceInFrame || (!frame.presented && !screenshot_)) {
                std::cerr << "Vulkan did not present the interface on the device\n";
                return 1;
            }
            if (!window_.present(frame)) {
                std::cerr << "Native window presentation failed\n";
                return 1;
            }
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

void App::handle(const WindowEvent& event) {
        if (event.type == WindowEvent::Type::FocusLoss) {
            stopPlaygroundDrag(); playgroundCamera_.clear(); playgroundRestoreCapture_=false;
            roomUi_.handle(event,captured_,view_.width,view_.height,room_);
            roomCamera_.clear();
            roomRestoreCapture_ = false;
            keys_.clear();
            endOrbit();
            leftDown_ = rightDown_ = false;
            window_.captureMouse(false);
            captured_ = false;
            restoreMouseCapture_ = false;
            return;
        }
        if (playgroundMode_) { handlePlayground(event); return; }
        if (testRoom_) {
            if (event.type==WindowEvent::Type::KeyDown && event.key=='p' && !event.repeat) enterPlayground();
            else handleRoom(event);
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
            if (overlay_ == Overlay::Cheats) {
                if (event.button != 1) return;
                const CheatMenuLayout menu = layoutCheatMenu(view_.width, view_.height);
                const int entry = cheatMenuAt(menu, static_cast<int>(std::lround(event.x)),
                                              static_cast<int>(std::lround(event.y)));
                if (entry == kCheatMenuClose) closeOverlay();
                else if (entry >= 0) cheat('1' + entry);
                return;
            }
            if (overlay_ != Overlay::None) return;
            // A click on the mode strip switches mode and goes no further: it must
            // not also become a walk order behind the strip, which is what a
            // clickable-but-transparent control would do in god mode.
            if (event.button == 1) {
                const int mode = modeBarAt(layoutModeBar(view_.width, view_.height), 
                                           static_cast<int>(std::lround(event.x)),
                                           static_cast<int>(std::lround(event.y)));
                if (mode >= 0) {
                    setMode(mode == 0 ? ViewMode::ThirdPerson
                                      : (mode == 1 ? ViewMode::God : ViewMode::FirstPerson));
                    return;
                }
            }
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
        else if (event.key == 'c') { if (!event.repeat) enterRoom(); }
        else if (event.key == 'p') { if (!event.repeat) enterPlayground(); }
        else if (event.key == 'h') openOverlay(Overlay::Help);
        else {
            keys_.insert(event.key);
            if (!event.repeat && (event.key == ' ' || event.key == kKeyLeftControl || event.key == kKeyAlt)) sendInput();
        }
    }

void App::cheat(int key) {
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

bool App::crosshairTarget(const ViewState& controls, gtc::Vec2& target, float& height) const {
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

void App::sendInput() {
        gtc::Vec2 move{};
        bool sprint = false, piss = false, crouch = false, grab = false, jump = false, block = false;
        const bool ranged = local() && local()->appearance.male && local()->unzipped;
        const ViewState control = controlView();
        float yaw = local() ? local()->yaw : control.yaw;
        view_.aiming = false;
        const bool gameControls = !testRoom_ && !playgroundMode_ && overlay_ == Overlay::None;
        crouch = gameControls && keys_.count(kKeyAlt) != 0;
        if (gameControls && view_.mode != ViewMode::God) {
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
        const bool hasAimTarget = !testRoom_ && !playgroundMode_ && ranged && view_.mode != ViewMode::God &&
            crosshairTarget(control, aimTarget, aimHeight);
        client_.sendInput(move, sprint, piss, yaw, control.pitch, slowWalk_, crouch, grab, jump, block,
                          aimTarget, aimHeight, hasAimTarget);
    }

void App::updateGod(float dt) {
        if (overlay_ != Overlay::None || view_.mode != ViewMode::God) return;
        const double forward = (keys_.count('w') ? 1 : 0) - (keys_.count('s') ? 1 : 0);
        const double right = (keys_.count('d') ? 1 : 0) - (keys_.count('a') ? 1 : 0);
        const double speed = dt * view_.godDistance * 1.05 * (keys_.count(kKeyShift) ? 1.6 : 1.0);
        view_.godX += static_cast<float>((-std::sin(view_.godYaw) * forward + std::cos(view_.godYaw) * right) * speed);
        view_.godZ += static_cast<float>((-std::cos(view_.godYaw) * forward - std::sin(view_.godYaw) * right) * speed);
        view_.godX = std::clamp(view_.godX, -9.0F, 9.0F);
        view_.godZ = std::clamp(view_.godZ, -7.0F, 7.0F);
    }

}  // namespace interactive_detail
}  // namespace gtc::native
