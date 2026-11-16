#pragma once

namespace {

// Isolate all sign-changing roots of a polynomial on [0, 1]. Its derivative's
// roots divide the interval into monotone pieces, so even a crossing followed by
// a return inside one tick cannot be missed. Long double avoids losing the very
// small distance of a nearly tangential rope catch to cancellation.
using RopePolynomial = std::array<long double, 7>;
struct RopeRoots { std::array<long double, 6> value{}; int count = 0; };

long double ropePolynomialAt(const RopePolynomial& coefficients, int degree, long double t) {
    long double value = coefficients[degree];
    for (int i = degree - 1; i >= 0; --i) value = value * t + coefficients[i];
    return value;
}

RopeRoots ropePolynomialRoots(const RopePolynomial& coefficients, int degree) {
    RopeRoots roots;
    while (degree > 0 && coefficients[degree] == 0.0L) --degree;
    if (degree == 0) return roots;
    if (degree == 1) {
        const long double t = -coefficients[0] / coefficients[1];
        if (t > 0.0L && t < 1.0L) roots.value[roots.count++] = t;
        return roots;
    }
    RopePolynomial derivative{};
    for (int i = 1; i <= degree; ++i) derivative[i - 1] = i * coefficients[i];
    const RopeRoots extrema = ropePolynomialRoots(derivative, degree - 1);
    const auto add = [&](long double t) {
        if (t <= 0.0L || t >= 1.0L || roots.count >= degree) return;
        if (roots.count == 0 || t - roots.value[roots.count - 1] > 1e-16L)
            roots.value[roots.count++] = t;
    };
    long double left = 0.0L, atLeft = ropePolynomialAt(coefficients, degree, left);
    for (int i = 0; i <= extrema.count; ++i) {
        const long double right = i < extrema.count ? extrema.value[i] : 1.0L;
        const long double atRight = ropePolynomialAt(coefficients, degree, right);
        if ((atLeft < 0.0L && atRight > 0.0L) || (atLeft > 0.0L && atRight < 0.0L)) {
            long double low = left, high = right;
            for (int pass = 0; pass < 80; ++pass) {
                const long double mid = 0.5L * (low + high);
                const long double value = ropePolynomialAt(coefficients, degree, mid);
                if ((atLeft < 0.0L && value < 0.0L) || (atLeft > 0.0L && value > 0.0L)) low = mid;
                else high = mid;
            }
            add(0.5L * (low + high));
        }
        if (atRight == 0.0L) add(right);
        left = right;
        atLeft = atRight;
    }
    return roots;
}

// The slack trajectory sampled below has p(t) = p0 + v0*t/(1+a*t) + g*t^2/2.
// Multiplying |p(t)-anchor|^2 - length^2 by (1+a*t)^2 gives a sixth-degree
// polynomial. Find its first inside-to-outside crossing, not merely its endpoint.
double ropeCatchTime(Vec3 arm, Vec3 velocity, double ropeLength, double gravity, double drag, double duration) {
    if (!(duration > 0.0)) return duration + 1.0;
    const double reach = length(arm);
    const double radial = reach > 0.0 ? dot(arm, velocity) / reach : 0.0;
    if (reach >= ropeLength - 1e-12 && radial > 1e-12) return 0.0;
    const double a = 0.5 * drag * length(velocity);
    const Vec3 g{0.0, -gravity, 0.0};
    const std::array<Vec3, 4> q{arm, velocity + arm * a, g * 0.5, g * (0.5 * a)};
    RopePolynomial coefficients{};
    const auto preciseDot = [](Vec3 x, Vec3 y) {
        return static_cast<long double>(x.x) * y.x + static_cast<long double>(x.y) * y.y +
               static_cast<long double>(x.z) * y.z;
    };
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) coefficients[i + j] += preciseDot(q[i], q[j]);
    const long double l2 = static_cast<long double>(ropeLength) * ropeLength;
    coefficients[0] -= l2;
    coefficients[1] -= 2.0L * a * l2;
    coefficients[2] -= static_cast<long double>(a) * a * l2;
    // A boundary point is exactly on the rope; do not let floating-point noise
    // turn an inward velocity into an immediate outward crossing.
    if (std::abs(reach - ropeLength) <= 1e-14 * std::max(1.0, ropeLength)) coefficients[0] = 0.0L;
    long double scale = 1.0L;
    for (int i = 1; i <= 6; ++i) { scale *= duration; coefficients[i] *= scale; }
    RopePolynomial derivative{};
    for (int i = 1; i <= 6; ++i) derivative[i - 1] = i * coefficients[i];
    const RopeRoots extrema = ropePolynomialRoots(derivative, 5);
    long double left = 0.0L;
    for (int i = 0; i <= extrema.count; ++i) {
        const long double right = i < extrema.count ? extrema.value[i] : 1.0L;
        if (ropePolynomialAt(coefficients, 6, right) > 0.0L) {
            long double low = left, high = right;
            for (int pass = 0; pass < 80; ++pass) {
                const long double mid = 0.5L * (low + high);
                if (ropePolynomialAt(coefficients, 6, mid) <= 0.0L) low = mid;
                else high = mid;
            }
            return duration * static_cast<double>(high);
        }
        left = right;
    }
    return duration + 1.0;
}

}  // namespace

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
    // The body solver already gathered contacts on the ball this tick. Keep
    // those contacts instead of discarding them before friction is resolved.
    for (std::size_t i = 0; i < kBodyCount; ++i) {
        ball.tickSpeed[i] = bodies_[i].velocity;
        ball.tickSpin[i] = bodies_[i].spin;
    }
    ball.force = {};
    addForce(ball.position, Vec3{0, -ball.mass * gravity, 0}, ForceKind::Gravity, &ball.force, ownerOf(ball));
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
    int events = 0;
    bool bent = false;
    double firstAt = 0.0;
    Vec3 firstPoint{};

    // Conservative advancement uses an upper bound on both the bob's speed and
    // every corner's speed. It cannot step past a face just because the arc is
    // curved. A micrometre is the room's contact resolution.
    for (int event = 0; event < 16 && elapsed < dt - 1e-12; ++event) {
        ++events;
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

        // At a tangential catch a distance/speed walk can take hundreds of tiny
        // steps and never reach the crossing. Solve the slack trajectory's first
        // outward intersection with the rope sphere instead.
        bool catchRope = false;
        if (!taut) {
            const double at = ropeCatchTime(state.position - anchor, state.velocity, ropeLength, gravity, drag, phase);
            if (at < first && at <= phase) { first = at; hitBody = -1; catchRope = true; }
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
        const double approach = dot(ball.velocity - bodyFaceSpeed(std::size_t(hitBody), contact), hitNormal);
        const bool sustained = taken < dt / 100.0 && std::abs(approach) <= playground_detail::kRestSpeed;
        ball.answered = true;
        ball.impacted = !sustained;
        ballContact(ball, hitNormal, hitBody, contact, 0.0, true, sustained, elapsed / dt, true);
        // Friction sees the body after the normal impulse, not its pre-impact speed.
        ball.tickSpeed[std::size_t(hitBody)] = body.velocity;
        ball.tickSpin[std::size_t(hitBody)] = body.spin;
        if (taut) {
            const Vec3 arm = ball.position - anchor;
            const double reach = length(arm);
            if (reach > 0.0 && dot(ball.velocity, arm / reach) < -playground_tick::kQuantumSpeed)
                rope_.taut = taut = false;
        }
        if (sustained) {
            // A maintained contact under gravity is not an endless sequence of
            // tiny impacts. Integrate the unspent time with constraints below.
            state = State{ball.position, ball.velocity};
            break;
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

    static int traced = 0;
    if (std::getenv("GTC_TRACE_ROPE_EVENTS") && bodyHeld(kTriangle) &&
        radius0 < ropeLength - 1e-4 && radius0 > ropeLength - 0.01 && traced++ < 3)
        std::printf("ROPE EVENTS %d elapsed %.12f of %.12f, start reach %.9f, end (%+.9f,%+.9f) v (%+.9f,%+.9f)\n",
                    events, elapsed, dt, radius0, state.position.x, state.position.y, state.velocity.x, state.velocity.y);
    bool constrainedStep = false;
    double finalRopeLoad = 0.0;
    Vec3 ropeTotal{};
    if (elapsed < dt - 1e-12) {
        // Resting contacts under a continuous force cannot be represented by
        // repeatedly bouncing at zero time. Spend the remaining time in bounded
        // steps and solve the rope, every touching shape, and their floor contacts
        // together. This is a velocity-constraint fallback, not a complete room
        // solver: the earlier broad-phase still routes ball/dot/floor meetings to
        // the legacy walk.
        constrainedStep = true;
        namespace pc = playground_constraints;
        constexpr int kSubsteps = 64;
        constexpr std::size_t kRows = 1 + kBodyCount * (1 + kMaxCorners);
        std::array<Vec3, kBodyCount> normalTotal{}, gripTotal{}, atBody{}, floorNormal{}, floorGrip{}, atFloor{};
        std::array<double, kBodyCount> rollTotal{};
        const double inertia = 0.4 * ball.mass * ball.radius * ball.radius;
        const auto localEnergy = [&]() {
            double energy = 0.5 * ball.mass * dot(state.velocity, state.velocity) +
                            0.5 * inertia * ball.spin * ball.spin + ball.mass * gravity * state.position.y;
            for (const Body& body : bodies_) {
                if (body.count == 0) continue;
                energy += 0.5 * body.mass * dot(body.velocity, body.velocity) +
                          0.5 * body.moment * body.spin * body.spin + body.mass * gravity * body.centre.y;
            }
            return energy;
        };
        const double initialEnergy = localEnergy();
        double projectionWork = 0.0;
        ball.touchCount = 0;  // the simultaneous solve below owns these contacts

        const auto constrain = [&](double step) {
            std::array<pc::Body, kBodyCount + 1> motion{};
            motion[0] = {state.position, state.velocity, ball.spin, 1.0 / ball.mass, 1.0 / inertia};
            for (std::size_t i = 0; i < kBodyCount; ++i) {
                const Body& body = bodies_[i];
                const bool fixed = bodyHeld(i);
                motion[i + 1] = {body.centre, fixed ? handSpeed_ : body.velocity, fixed ? 0.0 : body.spin,
                                 fixed || body.pivoted ? 0.0 : 1.0 / body.mass,
                                 fixed ? 0.0 : 1.0 / body.moment};
            }
            std::array<pc::Contact, kRows> rows{};
            std::size_t count = 0;
            for (std::size_t i = 0; i < kBodyCount; ++i) {
                Body& body = bodies_[i];
                if (body.count == 0) continue;
                const OutlineGap gap = outlineGap(state.position, body.position.data(), body.position.data(),
                                                  body.count, 1.0);
                const double spare = gap.gap - ball.radius;
                const Vec3 at = state.position - gap.normal * ball.radius;
                const double closing = dot(pc::pointVelocity(motion[0], at) - pc::pointVelocity(motion[i + 1], at),
                                           gap.normal);
                if (spare > quantum + std::max(0.0, -closing * step)) continue;
                pc::Contact& row = rows[count++];
                row.a = 0; row.b = i + 1; row.point = at; row.normal = gap.normal;
                row.target = spare > quantum ? -spare / step : 0.0;
                row.staticFriction = std::min(kBallStaticFriction, body.staticFriction);
                row.kineticFriction = std::min(kBallKineticFriction, body.kineticFriction);
                row.rollingFriction = kBallRollingResistance;
                row.rollingRadius = ball.radius;
                ball.touching = true;
                if (gap.normal.y > 0.0) ball.supportNormal = gap.normal;
                stopped[i] = true;
            }
            const Vec3 arm = state.position - anchor;
            const double reach = length(arm);
            if (reach > 0.0) {
                const Vec3 inward = arm * (-1.0 / reach);
                const double spare = ropeLength - reach;
                const double closing = dot(state.velocity, inward);
                if (spare <= quantum + std::max(0.0, -closing * step)) {
                    pc::Contact& row = rows[count++];
                    row.a = 0; row.point = state.position; row.normal = inward;
                    row.target = spare > quantum ? -spare / step : 0.0;
                }
            }
            for (std::size_t i = 0; i < kBodyCount; ++i) {
                const Body& body = bodies_[i];
                if (!stopped[i] || bodyHeld(i) || body.pivoted) continue;
                for (std::size_t j = 0; j < body.count; ++j) {
                    const Vec3 at = body.position[j];
                    const double spare = at.y - playground_tick::kSurfaceY;
                    const double closing = pc::pointVelocity(motion[i + 1], at).y;
                    if (spare > quantum + std::max(0.0, -closing * step)) continue;
                    pc::Contact& row = rows[count++];
                    row.a = i + 1; row.point = at; row.normal = {0, 1, 0};
                    row.target = spare > quantum ? -spare / step : 0.0;
                    row.staticFriction = body.staticFriction;
                    row.kineticFriction = body.kineticFriction;
                }
            }
            pc::solve(motion, rows, count);
            state.velocity = motion[0].velocity;
            ball.spin = motion[0].spin;
            for (std::size_t i = 0; i < kBodyCount; ++i) {
                Body& body = bodies_[i];
                if (bodyHeld(i) || !stopped[i]) continue;
                const bool moved = length(body.velocity - motion[i + 1].velocity) > 1e-12 ||
                                   std::abs(body.spin - motion[i + 1].spin) > 1e-12;
                body.velocity = motion[i + 1].velocity;
                body.spin = motion[i + 1].spin;
                if (moved && body.frozen) { body.frozen = false; body.restTicks = 0; }
            }
            double ropeLoad = 0.0;
            for (std::size_t j = 0; j < count; ++j) {
                const pc::Contact& row = rows[j];
                const Vec3 normal = row.normal * row.normalImpulse;
                const Vec3 tangent{row.normal.y, -row.normal.x, 0};
                const Vec3 grip = tangent * row.tangentImpulse;
                if (row.a == 0 && row.b == pc::kWorld) {
                    ropeTotal += normal;
                    ropeLoad += row.normalImpulse;
                } else if (row.a == 0) {
                    const std::size_t i = row.b - 1;
                    normalTotal[i] += normal; gripTotal[i] += grip; atBody[i] = row.point;
                    rollTotal[i] += row.rollingImpulse;
                } else {
                    const std::size_t i = row.a - 1;
                    floorNormal[i] += normal; floorGrip[i] += grip; atFloor[i] = row.point;
                }
            }
            return ropeLoad;
        };

        while (elapsed < dt - 1e-12) {
            const double step = std::min(dt / kSubsteps, dt - elapsed);
            state.velocity = damp(state.velocity, 0.5 * step) + Vec3{0, -0.5 * gravity * step, 0};
            const double firstLoad = constrain(step);
            state.position += state.velocity * step;
            ball.angle += ball.spin * step;
            for (std::size_t i = 0; i < kBodyCount; ++i) {
                Body& body = bodies_[i];
                if (!stopped[i] || bodyHeld(i) || body.frozen) continue;
                if (!body.pivoted) body.centre += body.velocity * step;
                body.angle += body.spin * step;
                placeCorners(body);
            }
            // Correct only geometrical drift, iterating because a rope and a
            // face can both be active. The velocity solve remains one-sided.
            const double beforeProjectionY = state.position.y;
            for (int pass = 0; pass < 16; ++pass) {
                double error = 0.0;
                const Vec3 arm = state.position - anchor;
                const double reach = length(arm);
                if (reach > ropeLength) {
                    const double extra = reach - ropeLength;
                    state.position -= arm * (extra / reach);
                    error = std::max(error, extra);
                }
                for (std::size_t i = 0; i < kBodyCount; ++i) {
                    const Body& body = bodies_[i];
                    if (body.count == 0) continue;
                    const OutlineGap gap = outlineGap(state.position, body.position.data(), body.position.data(),
                                                      body.count, 1.0);
                    const double depth = ball.radius - gap.gap;
                    if (depth > 0.0) {
                        state.position += gap.normal * depth;
                        error = std::max(error, depth);
                    }
                }
                if (error < 1e-12) break;
            }
            projectionWork += ball.mass * gravity * (state.position.y - beforeProjectionY);
            state.velocity = damp(state.velocity + Vec3{0, -0.5 * gravity * step, 0}, 0.5 * step);
            finalRopeLoad = (firstLoad + constrain(step)) / step;
            elapsed += step;
        }
        static int constraintTraces = 0;
        if (std::getenv("GTC_TRACE_CONSTRAINTS") && localEnergy() - initialEnergy > 1e-5 && constraintTraces++ < 25)
            std::printf("CONSTRAINT energy %+.8f J projection %+.8f J pos (%+.6f,%+.6f) v (%+.6f,%+.6f) spin %+.5f held %d\n",
                        localEnergy() - initialEnergy, projectionWork, state.position.x, state.position.y,
                        state.velocity.x, state.velocity.y, ball.spin, int(bodyHeld(kTriangle)));
        for (std::size_t i = 0; i < kBodyCount; ++i) {
            const Vec3 total = normalTotal[i] + gripTotal[i];
            ball.blows += total;
            if (length(normalTotal[i]) > 1e-12)
                addForce(atBody[i], normalTotal[i] / dt, ForceKind::Contact, nullptr, ownerOf(ball));
            if (length(gripTotal[i]) > 1e-12)
                addForce(atBody[i], gripTotal[i] / dt, ForceKind::Friction, nullptr, ownerOf(ball));
            if (!bodyHeld(i)) recordImpulse(atBody[i], total * -1.0, ForceKind::Ball, i);
            if (std::abs(rollTotal[i]) > 1e-12) {
                addCouple(atBody[i], rollTotal[i] / dt, {}, ForceKind::Rolling, ownerOf(ball));
                if (!bodyHeld(i)) addCouple(atBody[i], -rollTotal[i] / dt, {}, ForceKind::Rolling, i);
            }
            recordImpulse(atFloor[i], floorNormal[i], ForceKind::Contact, i);
            recordImpulse(atFloor[i], floorGrip[i], ForceKind::Friction, i);
        }
        if (length(ropeTotal) > 1e-12) ropeImpulse(ropeTotal);
    }

    ball.position = state.position;
    ball.velocity = state.velocity;
    const Vec3 arm = ball.position - anchor;
    const double reach = length(arm);
    rope_.taut = constrainedStep ? reach >= ropeLength - quantum && finalRopeLoad > 1e-9
                                 : taut && reach >= ropeLength - quantum && tension(state) > 0.0;
    rope_.tension = rope_.taut ? (constrainedStep ? finalRopeLoad : tension(state)) : 0.0;
    if (!constrainedStep && rope_.taut && reach > 0.0) {
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
