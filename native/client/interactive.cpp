#include "client/interactive.h"

#include "client/client.h"
#include "client/renderer.h"
#include "client/window.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <vector>

namespace gtc::native {
namespace {

enum class Overlay { None, Wheel, Cheats, Creator, Help };

struct CreatorEntry {
    std::string label;
    int minimum = 0;
    int maximum = 100;
    int Appearance::* integer = nullptr;
    bool Appearance::* boolean = nullptr;
    int morph = -1;
};

void circle(NativeFrame& frame, int cx, int cy, int radius, Color color) {
    for (int y = -radius; y <= radius; ++y) {
        const int half = static_cast<int>(std::sqrt(std::max(0, radius * radius - y * y)));
        frame.rectangle(cx - half, cy + y, half * 2 + 1, 1, color);
    }
}

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
        buildCreator();
        if (!window_.open("GRAND THEFT CYBER CRUSADER 1453 - Native", error)) {
            std::cerr << error << '\n';
            return 1;
        }
        window_.size(view_.width, view_.height);
        view_.gpu = window_.gpu();
        mouseX_ = view_.width * 0.5;
        mouseY_ = view_.height * 0.5;
        view_.yaw = 3.14159265F;
        view_.pitch = -0.34F;
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
            updatePresentation(dt);
            updateHighlight();
            const auto renderStart = Clock::now();
            if (!renderer_.render(client_.world(), visualState(), client_.playerId(), view_, frame, error)) {
                std::cerr << error << '\n';
                return 1;
            }
            const double renderMs = std::chrono::duration<double, std::milli>(Clock::now() - renderStart).count();
            view_.frameMilliseconds = view_.frameMilliseconds > 0 ? view_.frameMilliseconds * 0.9 + renderMs * 0.1 : renderMs;
            drawOverlay(frame);
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

    int review(const std::string& output) {
        std::string error;
        if (!client_.connect(options_.host, options_.port, error) || !client_.sendJoin(options_.name) ||
            !renderer_.load(options_.root, error) || !client_.receive(error)) {
            std::cerr << error << '\n';
            return 1;
        }
        buildCreator();
        if (const Character* citizen = local()) draft_ = citizen->appearance;
        view_.yaw = 3.14159265F;
        view_.pitch = -0.34F;
        NativeFrame base;
        if (!renderer_.render(client_.world(), client_.state(), client_.playerId(), view_, base, error) ||
            !base.save(output + "/base.png")) {
            std::cerr << error << '\n';
            return 1;
        }
        const std::pair<Overlay, const char*> captures[] = {{Overlay::Wheel, "wheel"},
            {Overlay::Cheats, "cheats"}, {Overlay::Creator, "creator"}, {Overlay::Help, "help"}};
        for (const auto& [overlay, name] : captures) {
            overlay_ = overlay;
            NativeFrame frame = base;
            drawOverlay(frame);
            if (!frame.save(output + "/" + name + ".png")) return 1;
        }
        std::cout << "native UI review: " << output << " (480x640)\n";
        return 0;
    }

private:
    const Character* local() const { return client_.state().character(client_.playerId()); }

    const ClientState& visualState() const {
        return presentationReady_ ? presentation_ : client_.state();
    }

    void updatePresentation(float dt) {
        const ClientState& latest = client_.state();
        if (!presentationReady_) {
            presentation_ = latest;
            presentationVersion_ = client_.stateVersion();
            presentationReady_ = true;
            return;
        }
        if (presentationVersion_ != client_.stateVersion()) {
            const ClientState previous = presentation_;
            presentation_ = latest;
            presentationVersion_ = client_.stateVersion();
            for (Character& character : presentation_.characters) {
                if (const Character* old = previous.character(character.id)) {
                    character.position = old->position;
                    character.yaw = old->yaw;
                }
            }
        }
        interpolateCharacters(latest, presentation_, dt);
    }

    void setMode(ViewMode mode) {
        if (view_.mode == mode) return;
        view_.mode = mode;
        keys_.clear();
        if (mode == ViewMode::God && captured_) {
            window_.captureMouse(false);
            captured_ = false;
        }
        if (mode == ViewMode::FirstPerson) view_.pitch = 0;
        if (mode == ViewMode::ThirdPerson) view_.pitch = -0.34F;
        if (mode == ViewMode::God) {
            if (const Character* citizen = local()) {
                view_.godX = citizen->position.x;
                view_.godZ = citizen->position.y;
            }
        }
    }

    void openOverlay(Overlay overlay) {
        if (overlay_ == overlay) {
            overlay_ = Overlay::None;
            return;
        }
        overlay_ = overlay;
        keys_.clear();
        window_.captureMouse(false);
        captured_ = false;
        if (overlay == Overlay::Creator) {
            if (const Character* citizen = local()) draft_ = citizen->appearance;
            selected_ = 0;
            scroll_ = 0;
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
        overlay_ = Overlay::None;
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
        const double scale = std::min(view_.width / 480.0, view_.height / 640.0);
        return std::hypot(sx - x, sy - y) <= 75 * scale;
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
            window_.captureMouse(false);
            captured_ = false;
            return;
        }
        if (event.type == WindowEvent::Type::MouseMove) {
            mouseX_ = event.x;
            mouseY_ = event.y;
            if (captured_) rotateView(view_, event.dx, event.dy);
            return;
        }
        if (event.type == WindowEvent::Type::Wheel) {
            if (view_.mode == ViewMode::God) view_.godDistance = static_cast<float>(std::clamp(view_.godDistance - event.wheel * 0.9, 8.0, 25.0));
            else view_.distance = static_cast<float>(std::clamp(view_.distance - event.wheel * 0.35, 2.4, 8.0));
            return;
        }
        if (event.type == WindowEvent::Type::MouseDown) {
            mouseX_ = event.x;
            mouseY_ = event.y;
            if (event.button != 1) return;
            if (overlay_ == Overlay::Wheel) {
                const double scale = std::min(1.0, std::min(view_.width / 480.0, view_.height / 640.0));
                const double x = (event.x - (view_.width - 480 * scale) * 0.5) / scale;
                const double y = (event.y - (view_.height - 640 * scale)) / scale;
                if (std::hypot(x - 240, y - 210) < 88) toiletChoice(1);
                else if (std::hypot(x - 140, y - 405) < 88) toiletChoice(2);
                else if (std::hypot(x - 340, y - 405) < 88) toiletChoice(3);
                return;
            }
            if (overlay_ != Overlay::None) return;
            if (view_.mode == ViewMode::God) {
                if (toiletHit(event.x, event.y)) interact();
                else {
                    gtc::Vec2 point;
                    if (ground(event.x, event.y, point)) client_.sendPath(point);
                }
            } else if (!captured_) {
                window_.captureMouse(true);
                captured_ = true;
            } else if (toiletHit(view_.width * 0.5, view_.height * 0.5)) {
                interact();
            }
            return;
        }
        if (event.type == WindowEvent::Type::KeyUp) {
            keys_.erase(event.key);
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
            else if (event.key == 'e' || event.key == kKeyEscape) overlay_ = Overlay::None;
            return;
        }
        if (overlay_ == Overlay::Cheats) {
            if (!event.repeat) cheat(event.key);
            return;
        }
        if (overlay_ == Overlay::Creator) {
            if (!event.repeat || (event.key != kKeyEnter && event.key != 'c' && event.key != kKeyEscape)) creatorKey(event.key);
            return;
        }
        if (overlay_ == Overlay::Help) {
            if (event.key == 'h' || event.key == kKeyEscape) overlay_ = Overlay::None;
            return;
        }
        if (event.key == kKeyEscape) {
            keys_.clear();
            window_.captureMouse(false);
            captured_ = false;
        } else if (event.key == '1') setMode(ViewMode::ThirdPerson);
        else if (event.key == '2') setMode(ViewMode::God);
        else if (event.key == '3') setMode(ViewMode::FirstPerson);
        else if (event.key == kKeyCapsLock) { if (!event.repeat) slowWalk_ = !slowWalk_; }
        else if (event.key == 'e') interact();
        else if (event.key == kKeyF1) openOverlay(Overlay::Cheats);
        else if (event.key == 'c') openOverlay(Overlay::Creator);
        else if (event.key == 'h') openOverlay(Overlay::Help);
        else keys_.insert(event.key);
    }

    void cheat(int key) {
        if (key == kKeyF1 || key == kKeyEscape) {
            overlay_ = Overlay::None;
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

    void sendInput() {
        gtc::Vec2 move{};
        bool sprint = false, piss = false;
        float yaw = local() ? local()->yaw : view_.yaw;
        if (overlay_ == Overlay::None && view_.mode != ViewMode::God) {
            const double forward = (keys_.count('w') ? 1 : 0) - (keys_.count('s') ? 1 : 0);
            const double right = (keys_.count('d') ? 1 : 0) - (keys_.count('a') ? 1 : 0);
            move = {static_cast<float>(std::sin(view_.yaw) * forward - std::cos(view_.yaw) * right),
                    static_cast<float>(std::cos(view_.yaw) * forward + std::sin(view_.yaw) * right)};
            sprint = keys_.count(kKeyShift) != 0;
            piss = view_.mode == ViewMode::FirstPerson && keys_.count('f') != 0;
            if (view_.mode == ViewMode::FirstPerson || forward != 0 || right != 0) yaw = view_.yaw;
        }
        view_.slowWalk = slowWalk_;
        client_.sendInput(move, sprint, piss, yaw, view_.pitch, slowWalk_);
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

    void buildCreator() {
        auto integer = [&](const std::string& label, int min, int max, int Appearance::* field) {
            creator_.push_back({label, min, max, field, nullptr, -1});
        };
        auto boolean = [&](const std::string& label, bool Appearance::* field) {
            creator_.push_back({label, 0, 1, nullptr, field, -1});
        };
        boolean("MALE", &Appearance::male);
        integer("SKIN", 0, 5, &Appearance::skin);
        integer("HAIR", 0, 15, &Appearance::hair);
        integer("HAIR COLOR", 0, 8, &Appearance::hairColor);
        integer("SHIRT", 0, 6, &Appearance::shirt);
        integer("PANTS", 0, 3, &Appearance::pants);
        integer("BEARD", 0, 2, &Appearance::beard);
        integer("EYE COLOR", 0, 5, &Appearance::eyeColor);
        integer("MAKEUP", 0, 4, &Appearance::makeup);
        integer("LIP COLOR", 0, 4, &Appearance::lipColor);
        integer("TIE", 0, 5, &Appearance::tie);
        boolean("COAT", &Appearance::coat);
        boolean("GLASSES", &Appearance::sunglasses);
        boolean("SUIT", &Appearance::suit);
        boolean("DRESS", &Appearance::dress);
        boolean("BERET", &Appearance::beret);
        boolean("EARRINGS", &Appearance::earrings);
        boolean("NECKLACE", &Appearance::necklace);
        boolean("FUR", &Appearance::fur);
        for (std::size_t i = 0; i < renderer_.morphSpecs(true).size(); ++i) {
            const MorphSpec& spec = renderer_.morphSpecs(true)[i];
            creator_.push_back({spec.key, spec.minimum, spec.maximum, nullptr, nullptr, static_cast<int>(i)});
        }
    }

    int creatorValue(const CreatorEntry& entry) const {
        if (entry.morph >= 0) return draft_.morphs[entry.morph];
        if (entry.boolean) return draft_.*entry.boolean ? 1 : 0;
        return draft_.*(entry.integer);
    }

    void creatorKey(int key) {
        if (key == 'c' || key == kKeyEscape) {
            overlay_ = Overlay::None;
            return;
        }
        if (key == kKeyEnter) {
            client_.sendCustomize(draft_);
            return;
        }
        if (key == kKeyUp) selected_ = std::max(0, selected_ - 1);
        if (key == kKeyDown) selected_ = std::min(static_cast<int>(creator_.size()) - 1, selected_ + 1);
        if (key == kKeyPageUp) selected_ = std::max(0, selected_ - 10);
        if (key == kKeyPageDown) selected_ = std::min(static_cast<int>(creator_.size()) - 1, selected_ + 10);
        if (key == kKeyLeft || key == kKeyRight) {
            CreatorEntry& entry = creator_[selected_];
            const int direction = key == kKeyRight ? 1 : -1;
            const int value = std::clamp(creatorValue(entry) + direction * (entry.morph >= 0 ? 5 : 1),
                                         entry.minimum, entry.maximum);
            if (entry.morph >= 0) draft_.morphs[entry.morph] = value;
            else if (entry.boolean) draft_.*entry.boolean = value != 0;
            else draft_.*(entry.integer) = value;
        }
        if (selected_ < scroll_) scroll_ = selected_;
        if (selected_ >= scroll_ + 10) scroll_ = selected_ - 9;
    }

    void drawOverlay(NativeFrame& frame) {
        if (overlay_ == Overlay::None) return;
        const Color panel = colorHex("#101713"), text = colorHex("#d7ff54"), muted = colorHex("#b4c5ba");
        if (overlay_ == Overlay::Wheel) {
            frame.rectangle(35, 65, 410, 495, panel);
            drawText(frame, 92, 85, "TOILET CHOICE", text);
            circle(frame, 240, 210, 85, colorHex("#334c42"));
            circle(frame, 140, 405, 85, colorHex("#4b4030"));
            circle(frame, 340, 405, 85, colorHex("#303f50"));
            circle(frame, 240, 325, 48, colorHex("#1b241f"));
            drawText(frame, 195, 200, "1 SIT", text);
            drawText(frame, 86, 395, "2 SHIT", text);
            drawText(frame, 286, 395, "3 PISS", text);
            drawText(frame, 204, 315, "E", muted);
            drawText(frame, 114, 505, local() && local()->appearance.male ? "PISS STANDING" : "PISS SEATED", muted);
        } else if (overlay_ == Overlay::Cheats) {
            frame.rectangle(20, 140, 440, 330, panel);
            drawText(frame, 125, 155, "F1 NEED CHEATS", text);
            drawText(frame, 38, 215, "1 BLADDER +25 2 -25", muted);
            drawText(frame, 38, 275, "3 EMPTY       4 FILL", muted);
            drawText(frame, 38, 335, "5 BOWEL +25   6 -25", muted);
            drawText(frame, 38, 395, "7 EMPTY       8 FILL", muted);
        } else if (overlay_ == Overlay::Help) {
            frame.rectangle(15, 70, 450, 530, panel);
            drawText(frame, 145, 85, "FIELD MANUAL", text);
            const char* lines[] = {"1 TPS 2 GOD 3 FPS", "CAPS SLOW SHIFT SPRINT", "CLICK CAPTURES MOUSE", "ESC RELEASES MOUSE",
                "E TOILET IN RANGE", "SIT SHIT PISS WHEEL", "HOLD F PISS IN FPS", "GOD CLICK PATHS",
                "GOD WASD FAST PANS", "C CREATOR F1 CHEATS", "F12 SAVES NATIVE PNG", "H CLOSES MANUAL"};
            for (int i = 0; i < 12; ++i) drawText(frame, 32, 140 + i * 37, lines[i], muted);
        } else if (overlay_ == Overlay::Creator) {
            frame.rectangle(0, 55, 480, 585, panel);
            drawText(frame, 24, 68, "C CREATOR ENTER APPLY", text);
            for (int row = 0; row < 10 && scroll_ + row < static_cast<int>(creator_.size()); ++row) {
                const int index = scroll_ + row;
                const int y = 120 + row * 40;
                if (index == selected_) frame.rectangle(8, y - 5, 464, 32, colorHex("#29382e"));
                drawText(frame, 15, y, creator_[index].label.substr(0, 17), index == selected_ ? text : muted);
                drawText(frame, 390, y, std::to_string(creatorValue(creator_[index])), text);
            }
            drawText(frame, 24, 540, "ARROWS EDIT PGUP PGDN", muted);
            drawText(frame, 24, 585, "ENTER SERVER CUSTOMIZE", muted);
        }
    }

    ClientState presentation_;
    std::uint64_t presentationVersion_ = 0;
    bool presentationReady_ = false;
    InteractiveOptions options_;
    NativeClient client_;
    WorldRenderer renderer_;
    NativeWindow window_;
    ViewState view_;
    Overlay overlay_ = Overlay::None;
    std::set<int> keys_;
    bool captured_ = false;
    bool slowWalk_ = false;
    bool screenshot_ = false;
    double mouseX_ = 240, mouseY_ = 320;
    std::vector<CreatorEntry> creator_;
    Appearance draft_;
    int selected_ = 0;
    int scroll_ = 0;
};

} // namespace

int runInteractive(const InteractiveOptions& options) {
    App app(options);
    return app.run();
}

int runInteractiveReview(const InteractiveOptions& options, const std::string& output) {
    App app(options);
    return app.review(output);
}

} // namespace gtc::native
