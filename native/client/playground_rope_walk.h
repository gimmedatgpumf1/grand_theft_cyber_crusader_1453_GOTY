#pragma once

// Included once by playground_rope.cpp, after the arc integrator. A rope bob
// near a body must meet it on the pendulum arc, not on a full tick of free fall
// followed by a projection back to the rope. This walk advances to the meeting,
// applies the ordinary ball/body impulse there, then spends the remaining time.
bool PlaygroundRoom::walkRopeContacts(Ball& ball) {
    const double dt = playground_tick::kTick;
    const double gravity = playground_tick::kGravity;
    const double quantum = playground_tick::kQuantum;
    const Vec3 anchor = rope_.anchor;
    const double ropeLength = rope_.length;
    const Vec3 arm0 = ball.position - anchor;
    const double radius0 = length(arm0);
    if (!(radius0 > 0.0) || radius0 > ropeLength + quantum) return false;

    // Leave ball/ball, dot and floor meetings to their existing contact walk.
    // The bound covers gravity and the other object's entire step this tick.
    const double speedBound = length(ball.velocity) + gravity * dt;
    const double travelBound = speedBound * dt;
    if (ball.position.y - ball.radius <= travelBound + quantum) return false;
    for (const Ball& other : balls_) {
        if (&other == &ball) continue;
        const double reach = ball.radius + other.radius + travelBound + length(other.position - other.previous);
        if (length(ball.position - other.previous) <= reach + quantum) return false;
    }
    if (length(ball.position - previous_) <= ball.radius + playground_detail::kPointRadius +
                                                 travelBound + length(point_ - previous_) + quantum)
        return false;

    // Only take over when a body could be reached this tick. This also avoids
    // false contacts from the ordinary walk's straight, unconstrained fall.
    bool nearby = false;
    for (std::size_t i = 0; i < kBodyCount; ++i) {
        const Body& body = bodies_[i];
        if (body.count == 0) continue;
        const OutlineGap gap = outlineGap(ball.position, body.previous.data(), body.previous.data(), body.count, 1.0);
        if (gap.gap < ball.radius - quantum) return false;  // placement recovery belongs to the ordinary walk
        double bodyTravel = 0.0;
        for (std::size_t j = 0; j < body.count; ++j)
            bodyTravel = std::max(bodyTravel, length(body.position[j] - body.previous[j]));
        nearby = nearby || gap.gap - ball.radius <= travelBound + bodyTravel + quantum;
    }
    if (!nearby) return false;

    struct State { Vec3 position{}, velocity{}; };
    const double drag = 0.5 * kAirDensity * kBallDragCoefficient * kPi * ball.radius * ball.radius / ball.mass;
    const auto damp = [drag](Vec3 speed, double time) {
        return speed * (1.0 / (1.0 + drag * length(speed) * time));
    };
    // Drag is split around a gravity/arc step; both halves only remove energy.
    const auto sample = [&](State from, bool taut, double time) {
        State out;
        const Vec3 startVelocity = damp(from.velocity, 0.5 * time);
        if (!taut) {
            out.position = from.position + startVelocity * time + Vec3{0, -0.5 * gravity * time * time, 0};
            out.velocity = damp(startVelocity + Vec3{0, -gravity * time, 0}, 0.5 * time);
            return out;
        }
        const Vec3 arm = from.position - anchor;
        const double theta = std::atan2(arm.x, -arm.y);
        const Vec3 tangent{std::cos(theta), std::sin(theta), 0};
        double angle = theta, omega = dot(startVelocity, tangent) / ropeLength;
        arcStep(theta, omega, gravity / ropeLength, time, angle, omega);
        out.position = anchor + Vec3{std::sin(angle), -std::cos(angle), 0} * ropeLength;
        out.velocity = damp(Vec3{std::cos(angle), std::sin(angle), 0} * (ropeLength * omega), 0.5 * time);
        return out;
    };
    const auto tension = [&](const State& state) {
        const Vec3 arm = state.position - anchor;
        const double angle = std::atan2(arm.x, -arm.y);
        const Vec3 tangent{std::cos(angle), std::sin(angle), 0};
        return tensionFor(ball.mass, ropeLength, dot(state.velocity, tangent) / ropeLength, angle, gravity);
    };
    const auto ropeImpulse = [&](Vec3 impulse) {
        if (length(impulse) <= 1e-12) return;
        addForce(ball.position, impulse * (1.0 / dt), ForceKind::Rope, nullptr, ownerOf(ball));
        addForce(anchor, impulse * (-1.0 / dt), ForceKind::Rope, nullptr, kAnchorOwner);
    };

    ball.began = ball.velocity;
    ball.previous = ball.position;
    ball.previousAngle = ball.angle;
    ball.bent = ball.answered = ball.impacted = false;
    ball.touching = false;
    ball.standing = true;
    ball.frozen = false;
    ball.alone = ball.velocity + Vec3{0, -gravity * dt, 0};
    ball.touchCount = 0;
    for (std::size_t i = 0; i < kBodyCount; ++i) {
        ball.tickSpeed[i] = bodies_[i].velocity;
        ball.tickSpin[i] = bodies_[i].spin;
    }
    addForce(ball.position, Vec3{0, -ball.mass * gravity, 0}, ForceKind::Gravity, nullptr, ownerOf(ball));
    ballAirDrag(ball, ball.velocity, 0.0);  // draw the air force; sample() integrates it

    State state{ball.position, ball.velocity};
    const Vec3 outward0 = arm0 / radius0;
    const double radial0 = dot(state.velocity, outward0);
    bool taut = radius0 >= ropeLength - quantum && radial0 >= -playground_tick::kQuantumSpeed &&
                tension(state) > 0.0;
    if (taut) {
        state.position = anchor + outward0 * ropeLength;
        if (radial0 > 0.0) {
            const Vec3 loss = outward0 * -radial0;
            state.velocity = state.velocity + loss;
            ropeImpulse(loss * ball.mass);
        }
    }
    std::array<bool, kBodyCount> stopped{};
    double elapsed = 0.0;
    bool bent = false;
    double firstAt = 0.0;
    Vec3 firstPoint{};

    // Conservative advancement uses an upper bound on both the bob's speed and
    // every corner's speed. It cannot step past a face just because the arc is
    // curved. A micrometre is the room's contact resolution.
    for (int event = 0; event < 16 && elapsed < dt - 1e-12; ++event) {
        const double left = dt - elapsed;
        double phase = left;
        bool willSlack = false;
        if (taut && tension(sample(state, true, left)) <= 0.0) {
            double low = 0.0, high = left;
            for (int pass = 0; pass < 40; ++pass) {
                const double middle = 0.5 * (low + high);
                if (tension(sample(state, true, middle)) > 0.0) low = middle;
                else high = middle;
            }
            phase = high;
            willSlack = true;
        }

        double first = phase + 1.0;
        int hitBody = -1;
        Vec3 hitNormal{};
        const double bobBound = length(state.velocity) + gravity * phase + 1e-9;
        for (std::size_t i = 0; i < kBodyCount; ++i) {
            const Body& body = bodies_[i];
            if (body.count == 0) continue;
            double cornerSpeed = 0.0;
            if (!stopped[i]) {
                for (std::size_t j = 0; j < body.count; ++j)
                    cornerSpeed = std::max(cornerSpeed, length(body.position[j] - body.previous[j]) / dt);
            }
            const double bound = bobBound + cornerSpeed;
            double at = 0.0;
            for (int pass = 0; pass < 320 && at <= phase; ++pass) {
                const State probe = sample(state, taut, at);
                const double fraction = stopped[i] ? 1.0 : std::clamp((elapsed + at) / dt, 0.0, 1.0);
                const OutlineGap gap = outlineGap(probe.position, body.previous.data(), body.position.data(),
                                                  body.count, fraction);
                const double spare = gap.gap - ball.radius;
                const Vec3 contact = probe.position - gap.normal * ball.radius;
                const double closing = dot(probe.velocity - bodyFaceSpeed(i, contact), gap.normal);
                if (spare <= 1e-8 && (closing < -1e-8 || spare < -quantum)) {
                    if (at < first) { first = at; hitBody = int(i); hitNormal = gap.normal; }
                    break;
                }
                if (!(bound > 0.0)) break;
                // When touching but separating, advance just enough to become
                // distinct; when clear, the distance/speed bound is conservative.
                const double advance = spare > 1e-8 ? 0.8 * (spare - 1e-8) / bound : quantum / (4.0 * bound);
                at += std::max(advance, 1e-10);
            }
        }

        // A slack rope catches the bob only when it reaches its length while
        // travelling outwards. The same conservative distance bound applies.
        bool catchRope = false;
        if (!taut) {
            double at = 0.0;
            for (int pass = 0; pass < 320 && at <= phase; ++pass) {
                const State probe = sample(state, false, at);
                const Vec3 arm = probe.position - anchor;
                const double reach = length(arm);
                const double spare = ropeLength - reach;
                const double radial = reach > 0.0 ? dot(probe.velocity, arm / reach) : 0.0;
                if (spare <= 1e-8 && radial > 1e-8) {
                    if (at < first) { first = at; hitBody = -1; catchRope = true; }
                    break;
                }
                if (!(bobBound > 0.0)) break;
                const double advance = spare > 1e-8 ? 0.8 * (spare - 1e-8) / bobBound : quantum / (4.0 * bobBound);
                at += std::max(advance, 1e-10);
            }
        }

        const double taken = std::min(first, phase);
        state = sample(state, taut, taken);
        ball.angle += ball.spin * taken;
        elapsed += taken;
        ball.position = state.position;
        ball.velocity = state.velocity;
        if (first > phase) {
            if (willSlack) { taut = false; continue; }
            break;
        }
        if (catchRope) {
            const Vec3 arm = state.position - anchor;
            const double reach = length(arm);
            if (reach > 0.0) {
                const Vec3 outward = arm / reach;
                state.position = anchor + outward * ropeLength;
                const double radial = dot(state.velocity, outward);
                if (radial > 0.0) {
                    const Vec3 loss = outward * -radial;
                    state.velocity = state.velocity + loss;
                    ropeImpulse(loss * ball.mass);
                }
            }
            taut = tension(state) > 0.0;
            continue;
        }
        if (hitBody < 0) break;
        Body& body = bodies_[std::size_t(hitBody)];
        if (!stopped[std::size_t(hitBody)] && !bodyHeld(std::size_t(hitBody)) && !body.pivoted) {
            const double when = keepAboveFloor(std::size_t(hitBody), body.previousCentre, body.previousAngle,
                                               body.centre, body.angle, elapsed / dt);
            body.centre = body.previousCentre + (body.centre - body.previousCentre) * when;
            body.angle = body.previousAngle + (body.angle - body.previousAngle) * when;
            placeCorners(body);
        }
        stopped[std::size_t(hitBody)] = true;
        const OutlineGap actual = outlineGap(ball.position, body.position.data(), body.position.data(), body.count, 1.0);
        hitNormal = actual.normal;
        const Vec3 contact = ball.position - hitNormal * ball.radius;
        if (!bent) { bent = true; firstAt = elapsed / dt; firstPoint = ball.position; }
        rope_.taut = taut;
        ball.answered = ball.impacted = true;
        ballContact(ball, hitNormal, hitBody, contact, 0.0, true, false, elapsed / dt, true);
        // Friction sees the body after the normal impulse, not its pre-impact speed.
        ball.tickSpeed[std::size_t(hitBody)] = body.velocity;
        ball.tickSpin[std::size_t(hitBody)] = body.spin;
        if (taut) {
            const Vec3 arm = ball.position - anchor;
            const double reach = length(arm);
            if (reach > 0.0 && dot(ball.velocity, arm / reach) < -playground_tick::kQuantumSpeed)
                rope_.taut = taut = false;
        }
        settleBallFriction(ball);
        answerSupportedShapes();
        state = State{ball.position, ball.velocity};
        if (taut) {
            const Vec3 arm = state.position - anchor;
            const double reach = length(arm);
            if (reach > 0.0) {
                const Vec3 outward = arm / reach;
                const double radial = dot(state.velocity, outward);
                if (radial > 0.0) {
                    const Vec3 loss = outward * -radial;
                    state.velocity = state.velocity + loss;
                    ropeImpulse(loss * ball.mass);
                } else if (radial < -playground_tick::kQuantumSpeed) {
                    taut = false;
                }
            }
            if (tension(state) <= 0.0) taut = false;
        }
    }

    ball.position = state.position;
    ball.velocity = state.velocity;
    const Vec3 arm = ball.position - anchor;
    const double reach = length(arm);
    rope_.taut = taut && reach >= ropeLength - quantum && tension(state) > 0.0;
    rope_.tension = rope_.taut ? tension(state) : 0.0;
    if (rope_.taut && reach > 0.0) {
        const Vec3 pull = arm * (-rope_.tension / reach);
        addForce(ball.position, pull, ForceKind::Rope, nullptr, ownerOf(ball));
        addForce(anchor, pull * -1.0, ForceKind::Rope, nullptr, kAnchorOwner);
    }
    if (bent) {
        ball.bent = true;
        ball.bendAt = firstAt;
        ball.bendPoint = firstPoint;
        ball.bendIn = firstAt > 0.0 ? (firstPoint - ball.previous) / (dt * firstAt) : Vec3{};
        ball.bendOut = firstAt < 1.0 ? (ball.position - firstPoint) / (dt * (1.0 - firstAt)) : Vec3{};
    }
    return true;
}
