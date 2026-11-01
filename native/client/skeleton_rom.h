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
        {"temporomandibular", {0, 25}},
        {"atlanto_occipital", {-10, 10}},
        {"atlanto_axial", {-35, 35}}, // primary axial rotation
        {"sternoclavicular", {-10, 35}}, // elevation/depression
        {"acromioclavicular", {-5, 10}}, // approximation of 15-degree total elevation
        {"radioulnar_pivot", {-80, 80}},
        {"thumb_cmc_saddle", {-15, 35}},
        {"shoulder_ball_socket", {-30, 120}}, // abduction/adduction at glenohumeral joint
        {"elbow_hinge", {0, 150}},
        {"wrist_condyloid", {-70, 80}},
        {"thumb_mcp", {0, 50}},
        {"thumb_ip_hinge", {0, 80}},
        {"finger_mcp_condyloid", {-30, 90}},
        {"finger_pip_hinge", {0, 100}},
        {"finger_dip_hinge", {-20, 90}},
        {"hip_ball_socket", {-20, 120}},
        {"hip_abduction", {-30, 45}}, // adduction / abduction
        {"hip_rotation", {-45, 45}},
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
    if (kind == "intervertebral") {
        // Individual-level population references are small; never apply the
        // complete spine's ROM to every vertebra. These are approximations.
        if ((!first.empty() && first[0]=='C') || (!second.empty() && second[0]=='C')) {
            // Published segment totals; symmetric split is an approximation.
            const auto has = [&](const char* a, const char* b) {
                return (first==a && second==b) || (first==b && second==a);
            };
            if (has("C2","C3")) return ReferenceBendRange{-4,4};
            if (has("C3","C4")) return ReferenceBendRange{-6.5,6.5};
            if (has("C4","C5")) return ReferenceBendRange{-9.5,9.5};
            if (has("C5","C6")) return ReferenceBendRange{-8.5,8.5};
            if (has("C6","C7")) return ReferenceBendRange{-8,8};
            return ReferenceBendRange{-5,5};
        }
        if ((!first.empty() && first[0]=='T') || (!second.empty() && second[0]=='T')) return ReferenceBendRange{-5,5};
        return ReferenceBendRange{-5,9};
    }
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
