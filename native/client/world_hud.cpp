// The 2D overlay for the world view.

#include "client/world_hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace gtc::native {

void drawLabels(NativeFrame& frame, const Camera& camera, const ClientState& state, std::uint32_t localId) {
    if (std::getenv("GTC_LABELDEBUG")) {
        std::fprintf(stderr, "hud state=%p chars=%p size=%zu sizeof=%zu\n", (const void*)&state,
                     (const void*)state.characters.data(), state.characters.size(), sizeof(Character));
    }
    for (const Character& character : state.characters) {
        if (character.id == localId) continue;
        const double labelHeight = character.prone ? 0.65 : character.crouched ? 1.35 : 2.08;
        const Vec3 world{character.position.x, character.elevation + labelHeight, character.position.y};
        if (length(camera.position - world) < 1.6) continue;
        const Vec3 point = camera.view(world);
        if (point.z <= 0.2) continue;
        const double sx = camera.width * 0.5 + camera.focal() * point.x / point.z;
        const double sy = camera.height * 0.5 - camera.focal() * point.y / point.z;
        if (sx < 20 || sy < 40 || sx >= camera.width - 20 || sy >= camera.height - 40) continue;
        const std::string label = std::string(character.ai ? "AI " : "") + character.name.substr(0, 18);
        const int width = std::min(250, std::max(64, static_cast<int>(label.size()) * 13 + 18));
        const int x = std::clamp(static_cast<int>(sx) - width / 2, 4, std::max(4, frame.width - width - 4));
        const int y = std::clamp(static_cast<int>(sy) - 36, 64, std::max(64, frame.height - 90));
        frame.blendRectangle(x, y, width, 36, colorHex("#07100c"), 0.84);
        frame.rectangle(x, y, 3, 36, colorHex(character.color.c_str()));
        drawText(frame, x + 9, y + 4, label, colorHex(character.color.c_str()));
    }
}

namespace {

void panel(NativeFrame& frame, int x, int y, int width, int height, bool accent = false) {
    frame.blendRectangle(x + 3, y + 4, width, height, colorHex("#020604"), 0.42);
    frame.blendRectangle(x, y, width, height, colorHex("#0b1411"), 0.86);
    frame.rectangle(x, y, width, 2, colorHex("#52665d"));
    frame.rectangle(x, y, 2, height, colorHex(accent ? "#d7ff54" : "#33483e"));
}

void drawMinimap(NativeFrame& frame, const ClientWorld& world, const ClientState& state,
                 std::uint32_t localId, int x) {
    const int y = 112, width = 132, height = 104;
    frame.rectangle(x, y, width, height, colorHex("#07100b"));
    const double scale = std::min((width - 12.0) / world.width, (height - 12.0) / world.depth);
    auto project = [&](double px, double pz, int& sx, int& sy) {
        sx = x + width / 2 + static_cast<int>(px * scale);
        sy = y + height / 2 + static_cast<int>(pz * scale);
    };
    for (const Obstacle& obstacle : world.obstacles) {
        if (obstacle.kind == "boundary") continue;
        int sx = 0, sy = 0;
        project(obstacle.center.x, obstacle.center.y, sx, sy);
        const int w = std::max(2, static_cast<int>(obstacle.halfSize.x * 2 * scale));
        const int h = std::max(2, static_cast<int>(obstacle.halfSize.y * 2 * scale));
        frame.rectangle(sx - w / 2, sy - h / 2, w, h,
                        colorHex(obstacle.kind == "toilet" ? "#d7ff54" : "#526158"));
    }
    for (const Puddle& puddle : state.puddles) {
        int sx = 0, sy = 0;
        project(puddle.position.x, puddle.position.y, sx, sy);
        frame.rectangle(sx - 1, sy - 1, 3, 3, colorHex("#b5a636"));
    }
    for (const Character& character : state.characters) {
        int sx = 0, sy = 0;
        project(character.position.x, character.position.y, sx, sy);
        const Color color = character.id == localId ? colorHex("#d7ff54") :
            character.ai ? colorHex("#ff765f") : colorHex(character.color.c_str());
        frame.rectangle(sx - 2, sy - 2, character.id == localId ? 5 : 4, character.id == localId ? 5 : 4, color);
        frame.rectangle(sx + static_cast<int>(std::sin(character.yaw) * 5),
                        sy + static_cast<int>(std::cos(character.yaw) * 5), 2, 2, color);
    }
}

}  // namespace

void drawCrosshair(NativeFrame& frame, const ViewState& view) {
    if (view.mode == ViewMode::God) return;
    int cx = frame.width / 2, cy = frame.height / 2;
    if (view.orbitPreview) { cx = static_cast<int>(std::lround(view.reticleX)); cy = static_cast<int>(std::lround(view.reticleY)); }
    cx = std::clamp(cx, 12, frame.width - 13); cy = std::clamp(cy, 12, frame.height - 13);
    const int unit = std::clamp(static_cast<int>(std::lround(std::min(frame.width, frame.height) / 640.0)), 1, 4);
    const int gap = 3 * unit, arm = 7 * unit, length = arm - gap + 1;
    const int x = cx - unit / 2, y = cy - unit / 2;
    const double scale = frame.uiScale, offsetX = frame.uiOffsetX, offsetY = frame.uiOffsetY;
    frame.uiScale = 1.0; frame.uiOffsetX = frame.uiOffsetY = 0.0;
    const Color shadow = colorHex("#020604"), ink = colorHex("#d7ff54");
    const auto stroke = [&](int sx, int sy, int width, int height, bool outline) {
        const int border = outline ? unit : 0;
        frame.rectangle(sx - border, sy - border, width + 2 * border, height + 2 * border,
                        outline ? shadow : ink);
    };
    for (int pass = 0; pass < 2; ++pass) {
        const bool outline = pass == 0;
        stroke(x, y, unit, unit, outline);
        stroke(cx - arm, y, length, unit, outline);
        stroke(cx + gap, y, length, unit, outline);
        stroke(x, cy - arm, unit, length, outline);
        stroke(x, cy + gap, unit, length, outline);
    }
    frame.uiScale = scale; frame.uiOffsetX = offsetX; frame.uiOffsetY = offsetY;
}

double hudMilliseconds = 0.0;

double lastHudMilliseconds() { return hudMilliseconds; }

void recordHudMilliseconds(double value) { hudMilliseconds = value; }

void drawHud(NativeFrame& frame, const ClientWorld& world, const ClientState& state,
             std::uint32_t localId, const ViewState& view) {
    const Character* local = state.character(localId);
    const Color text = colorHex("#d7ff54"), muted = colorHex("#b4c5ba"), cyan = colorHex("#6edbd2");
    const double centeredX = frame.uiOffsetX;
    panel(frame, 0, 0, 640, 60, true);
    drawText(frame, 12, 5, "GTC // 1453", text);
    drawText(frame, 12, 34, "PORCELAIN PROTOCOL", muted);
    const int minuteOfDay = static_cast<int>(state.gameTime / 60.0) % 1440;
    const auto two = [](int value) { return (value < 10 ? "0" : "") + std::to_string(value); };
    drawText(frame, 282, 5, "DAY " + std::to_string(state.day) + " " + two(minuteOfDay / 60) + ":" +
        two(minuteOfDay % 60), text);
    drawText(frame, 350, 34, "15X TIME", muted);
    drawText(frame, 476, 5, "NET // ONLINE", text);
    std::string performance = std::to_string(state.characters.size()) + " CIT / " + (view.gpu ? "GPU" : "CPU");
    if (view.fps > 0) {
        performance += " " + std::to_string(static_cast<int>(view.fps + 0.5));
        char breakdown[64];
        std::snprintf(breakdown, sizeof(breakdown), " d%.1f p%.1f l%.1f", view.drawMilliseconds,
                      view.presentMilliseconds, view.logicMilliseconds);
        performance += breakdown;
    }
    drawText(frame, 476, 34, performance, muted);
    if (local) {
        frame.uiOffsetX = 0.0;
        panel(frame, 12, 74, 218, 174, true);
        drawText(frame, 24, 82, "MEAT " + std::to_string(static_cast<int>(local->health)) + " HP", text);
        drawText(frame, 24, 112, local->name.substr(0, 15), muted);
        drawText(frame, 24, 145, "BLADDER " + std::to_string(static_cast<int>(local->needs.bladder)) + "%", cyan);
        frame.rectangle(24, 174, 194, 8, colorHex("#29332d"));
        frame.rectangle(24, 174, static_cast<int>(194 * std::clamp(local->needs.bladder / 100.0F, 0.0F, 1.0F)), 8, cyan);
        drawText(frame, 24, 191, "BOWEL " + std::to_string(static_cast<int>(local->needs.bowel)) + "%", colorHex("#ffcc62"));
        frame.rectangle(24, 220, 194, 8, colorHex("#29332d"));
        frame.rectangle(24, 220, static_cast<int>(194 * std::clamp(local->needs.bowel / 100.0F, 0.0F, 1.0F)), 8,
                        colorHex("#ffcc62"));
        frame.uiOffsetX = frame.width - 640 * frame.uiScale;
        panel(frame, 478, 74, 150, 162);
        drawText(frame, 488, 82, "SECTOR 01", text);
        drawMinimap(frame, world, state, localId, 490);
        drawText(frame, 488, 222, "LOCAL / AI", muted);
        frame.uiOffsetX = centeredX;
        frame.uiOffsetY = (frame.height - 480 * frame.uiScale) * 0.5;
        if (local->grabbing) {
            panel(frame, 220, 264, 200, 34, true);
            drawText(frame, 232, 270, "W CLIMB / S DROP", text);
        } else if (local->charge > 0.001F) {
            const Color chargeColor = local->fullChargeTime > 1.0F ? colorHex("#ff765f") : text;
            panel(frame, 220, 264, 200, 34, local->fullChargeTime > 0.0F);
            drawText(frame, 232, 266, "JUMP " + std::to_string(static_cast<int>(local->charge * 100 + 0.5F)) + "%", chargeColor);
            frame.rectangle(228, 292, 184, 3, colorHex("#29332d"));
            frame.rectangle(228, 292, static_cast<int>(184 * local->charge), 3, chargeColor);
        }
        frame.uiOffsetY = std::max(0.0, frame.height - 480 * frame.uiScale);
        panel(frame, 12, 302, 616, 44);
        if (!local->notice.empty()) drawText(frame, 24, 310, local->notice.substr(0, 52), muted);
        else if (!state.events.empty()) drawText(frame, 24, 310, state.events.back().text.substr(0, 52), muted);
        else drawText(frame, 24, 310, "QUEST // ANSWER NATURE'S CALL", text);
        // The two are different vector types (client-space and simulation-space),
        // so the distance is taken component-wise rather than through the helper.
        const double range = std::hypot(local->position.x - world.toiletFront.x,
                                        local->position.y - world.toiletFront.y);
        panel(frame, 12, 354, 616, 44, range <= world.interactionRange || local->toiletAction != ToiletAction::None);
        if (local->toiletAction != ToiletAction::None) drawText(frame, 24, 362, "E // STAND UP", text);
        else if (range <= world.interactionRange) drawText(frame, 24, 362, "E // CHOOSE ACTION", text);
        else drawText(frame, 24, 362, "MOVE CLOSER TO INTERACT", muted);
    }
    frame.uiOffsetX = centeredX;
    frame.uiOffsetY = std::max(0.0, frame.height - 480 * frame.uiScale);
    panel(frame, 0, 408, 640, 72);
    drawText(frame, 12, 416, "1 THIRD", view.mode == ViewMode::ThirdPerson ? text : muted);
    drawText(frame, 132, 416, "2 GOD", view.mode == ViewMode::God ? text : muted);
    drawText(frame, 232, 416, "3 FIRST", view.mode == ViewMode::FirstPerson ? text : muted);
    if (local && view.mode != ViewMode::God) {
        const char* stance = local->blocking ? "BLOCK" : view.aiming ? "AIM" : local->prone ? "PRONE" :
            local->grabbing ? "GRAB" : local->crouched ? "CROUCH" : local->unzipped ? "UNZIP" :
            !local->grounded ? "AIR" : view.slowWalk ? "SLOW" : "FAST";
        drawText(frame, 520, 416, stance, muted);
    }
    if (!local) drawText(frame, 12, 448, "WAITING FOR CITIZEN", text);
    else if (view.mode == ViewMode::God) drawText(frame, 12, 448, "RMB GO / SHIFT RUN / 2X ADV / LMB ATTACK", muted);
    else drawText(frame, 12, 448, "WASD / SPACE JUMP / CTRL GRAB / ALT CROUCH", muted);
}


}  // namespace gtc::native
