#pragma once

// Conventional clinical reference angles, in degrees from anatomical neutral.
// Individual ranges differ. These are not offsets from the registered CT pose.
// Sources and the movements represented are documented in JOINT_MOBILITY.md.
#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace gtc::native {
struct ReferenceBendRange {
    double extension, flexion;
};

inline std::optional<ReferenceBendRange> referenceBendRange(std::string_view kind) {
    struct Entry { std::string_view kind; ReferenceBendRange range; };
    static constexpr Entry entries[] = {
        {"shoulder_ball_socket", {-60, 120}}, // glenohumeral flexion, not whole shoulder
        {"elbow_hinge", {0, 150}},
        {"wrist_condyloid", {-70, 80}},
        {"thumb_mcp", {0, 50}},
        {"thumb_ip_hinge", {0, 80}},
        {"finger_mcp_condyloid", {-30, 90}},
        {"finger_pip_hinge", {0, 100}},
        {"finger_dip_hinge", {-20, 90}},
        {"hip_ball_socket", {-20, 120}},
        {"knee_modified_hinge", {0, 135}},
        {"ankle_hinge", {-20, 50}}, // negative dorsiflexion, positive plantarflexion
        {"great_toe_mtp", {-70, 45}},
        {"lesser_toe_mtp", {-40, 40}},
    };
    for (const auto& e : entries) if (e.kind == kind) return e.range;
    return std::nullopt; // no invented number for joints without a cited range
}

inline std::optional<ReferenceBendRange> referenceBendRange(
    std::string_view kind, std::string_view first, std::string_view second) {
    if (kind != "toe_ip_hinge") return referenceBendRange(kind);
    const auto has = [&](std::string_view fragment) {
        return first.find(fragment) != std::string_view::npos || second.find(fragment) != std::string_view::npos;
    };
    if (has("FootProxP1")) return ReferenceBendRange{0,50};
    if (has("FootMidP")) {
        if (has("FootDistP")) return ReferenceBendRange{-30,60};
        if (has("FootProxP")) return ReferenceBendRange{0,35};
    }
    return std::nullopt;
}

inline std::optional<double> clampReferenceBend(std::string_view kind, double degrees,
                                                 std::string_view first = {}, std::string_view second = {}) {
    const auto range = referenceBendRange(kind, first, second);
    if (!range || !std::isfinite(degrees)) return std::nullopt;
    return std::clamp(degrees, range->extension, range->flexion);
}
} // namespace gtc::native
