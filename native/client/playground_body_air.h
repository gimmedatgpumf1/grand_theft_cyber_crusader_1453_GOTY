#pragma once

#include "client/playground_ball_shape.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <cstddef>

namespace gtc::native::playground_body_air {

// Apply the same face-pressure drag law over any interval. The callback gets
// each face's actual impulse and the undamped force, so a caller can either
// draw the original force or accumulate the substeps' impulse for the ledger.
template <typename Body, typename OnImpulse>
void apply(Body& body, double seconds, OnImpulse onImpulse) {
    if (!(seconds > 0.0) || body.count == 0) return;
    const auto pointSpeed = [&](Vec3 point) {
        const Vec3 arm = point - body.centre;
        return body.velocity + Vec3{-body.spin * arm.y, body.spin * arm.x, 0};
    };
    const auto pressure = [&](double span, double normalSpeed) {
        return 0.5 * ball_shape::kAirDensity * playground_detail::kPlateDragCoefficient *
               span * playground_detail::kRoomDepth * normalSpeed * normalSpeed;
    };
    Vec3 total{};
    double fastest = length(body.velocity);
    for (std::size_t i = 0; i < body.count; ++i) {
        const Vec3 from = body.position[i];
        const Vec3 along = body.position[(i + 1) % body.count] - from;
        const double span = length(along);
        if (!(span > 0.0)) continue;
        const Vec3 out{along.y / span, -along.x / span, 0};
        const Vec3 speed = pointSpeed(from + along * 0.5);
        const double vn = dot(speed, out);
        fastest = std::max(fastest, length(speed));
        if (vn < 0.0) total += out * pressure(span, vn);
    }
    double share = 1.0;
    if (length(total) > 0.0 && fastest > 0.0)
        share = 1.0 / (1.0 + length(total) * seconds / (body.mass * fastest));
    for (std::size_t i = 0; i < body.count; ++i) {
        const Vec3 from = body.position[i];
        const Vec3 along = body.position[(i + 1) % body.count] - from;
        const double span = length(along);
        if (!(span > 0.0)) continue;
        const Vec3 out{along.y / span, -along.x / span, 0};
        const Vec3 at = from + along * 0.5;
        const double vn = dot(pointSpeed(at), out);
        if (!(vn < 0.0)) continue;
        const Vec3 air = out * pressure(span, vn);
        const Vec3 impulse = air * (seconds * share);
        const Vec3 arm = at - body.centre;
        onImpulse(at, impulse, air);
        if (!body.pivoted) body.velocity += impulse * (1.0 / body.mass);
        body.spin += (arm.x * impulse.y - arm.y * impulse.x) / body.moment;
    }
}

}  // namespace gtc::native::playground_body_air
