#pragma once

#include "client/playground_rope_math.h"

#include <cstdint>


// Included once by playground_rope.cpp, after the arc integrator. A rope bob
// near a body must meet it on the pendulum arc, not on a full tick of free fall
// followed by a projection back to the rope. This walk advances to the meeting,
// applies the ordinary ball/body impulse there, then spends the remaining time.
bool PlaygroundRoom::walkRopeContacts(Ball& ball) {
    const double dt = playground_tick::kTick;
    const double gravity = playground_tick::kGravity;
    static std::uint64_t debugTick = 0;
    const std::uint64_t thisDebugTick = debugTick++;
    const char* traceSetting = std::getenv("GTC_TRACE_TICK");
    std::uint64_t requestedTraceTick = 0;
    bool traceAllTicks = traceSetting && traceSetting[0] == 'a';
    if (traceSetting && !traceAllTicks)
        for (const char* digit = traceSetting; *digit >= '0' && *digit <= '9'; ++digit)
            requestedTraceTick = requestedTraceTick * 10 + static_cast<unsigned>(*digit - '0');
    const bool traceTick = traceSetting && (traceAllTicks || requestedTraceTick == thisDebugTick);
    if (traceTick) {
        std::printf("\\n=== ROPE TICK %llu dt %.9g ===\\n", static_cast<unsigned long long>(thisDebugTick), dt);
        std::printf("BALL mass %.9g radius %.9g p(%.9g,%.9g) v(%.9g,%.9g) spin %.9g omega KE %.12g PE %.12g\\n",
                    ball.mass, ball.radius, ball.position.x, ball.position.y, ball.velocity.x, ball.velocity.y,
                    ball.spin, 0.5 * ball.mass * dot(ball.velocity, ball.velocity),
                    ball.mass * gravity * ball.position.y);
        for (std::size_t i = 0; i < kBodyCount; ++i) {
            const Body& b = bodies_[i];
            if (b.count == 0) continue;
            std::printf("BODY %zu mass %.9g I %.9g p(%.9g,%.9g) v(%.9g,%.9g) spin %.9g KE %.12g PE %.12g\\n",
                        i, b.mass, b.moment, b.centre.x, b.centre.y, b.velocity.x, b.velocity.y, b.spin,
                        0.5 * b.mass * dot(b.velocity, b.velocity) + 0.5 * b.moment * b.spin * b.spin,
                        b.mass * gravity * b.centre.y);
        }
    }
    const double quantum = playground_tick::kQuantum;
    const Vec3 anchor = rope_.anchor;
    const double ropeLength = rope_.length;
    const Vec3 arm0 = ball.position - anchor;
    const double radius0 = length(arm0);
    if (!(radius0 > 0.0) || radius0 > ropeLength + quantum) {
        if (std::getenv("GTC_TRACE_HELD") && bodyHeld(kTriangle))
            std::printf("ROPE REJECT reach %.9f > %.9f\n", radius0, ropeLength);
        return false;
    }

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
        if (gap.gap < ball.radius - quantum) {
            static int rejected = 0;
            if (std::getenv("GTC_TRACE_HELD") && bodyHeld(kTriangle) && rejected++ < 30)
                std::printf("ROPE REJECT overlap body %zu depth %.9f reach %.9f v (%+.6f,%+.6f)\n",
                            i, ball.radius - gap.gap, radius0, ball.velocity.x, ball.velocity.y);
            return false;  // placement recovery belongs to the ordinary walk
        }
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

    // The shapes have already been predicted through the whole tick. Save that
    // prediction before the ball can apply an impact, so a later substep can
    // remove the unspent share of the prediction without undoing the impact.
    std::array<Vec3, kBodyCount> predictedVelocity{};
    std::array<double, kBodyCount> predictedSpin{};
    for (std::size_t i = 0; i < kBodyCount; ++i) {
        predictedVelocity[i] = bodies_[i].velocity;
        predictedSpin[i] = bodies_[i].spin;
    }
    const std::size_t predictedForces = forceCount_;
    if (traceTick) {
        for (std::size_t f = 0; f < predictedForces; ++f) {
            const ForceSample& x = forces_[f];
            std::printf("FORCE owner %zu kind %u at(%.9g,%.9g) F(%.9g,%.9g) Nm %.9g\\n",
                        x.owner, static_cast<unsigned>(x.kind), x.at.x, x.at.y, x.force.x, x.force.y, x.moment);
        }
    }

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
        bool uncertain = false;
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
            int pass = 0;
            for (; pass < 320 && at <= phase; ++pass) {
                const State probe = sample(state, taut, at);
                const double fraction = stopped[i] ? 1.0 : std::clamp((elapsed + at) / dt, 0.0, 1.0);
                const OutlineGap gap = outlineGap(probe.position, body.previous.data(), body.position.data(),
                                                  body.count, fraction);
                const double spare = gap.gap - ball.radius;
                const Vec3 contact = probe.position - gap.normal * ball.radius;
                const double closing = dot(probe.velocity - bodyFaceSpeed(i, contact), gap.normal);
                if (spare <= quantum && (closing < -1e-8 || spare < -quantum)) {
                    if (at < first) { first = at; hitBody = int(i); hitNormal = gap.normal; uncertain = false; }
                    break;
                }
                if (!(bound > 0.0)) break;
                // When touching but separating, advance just enough to become
                // distinct; when clear, the distance/speed bound is conservative.
                const double advance = spare > quantum ? 0.8 * (spare - quantum) / bound : quantum / (4.0 * bound);
                at += std::max(advance, 1e-10);
            }
            // Exhausting the conservative walk is not proof that the rest of
            // the path is clear, especially at a nearly tangential approach.
            if (pass == 320 && at < phase && at < first) {
                first = at; hitBody = -1; uncertain = true;
            }
        }

        // At a tangential catch a distance/speed walk can take hundreds of tiny
        // steps and never reach the crossing. Solve the slack trajectory's first
        // outward intersection with the rope sphere instead.
        bool catchRope = false;
        if (!taut) {
            const double at = ropeCatchTime(state.position - anchor, state.velocity, ropeLength, gravity, drag, phase);
            if (at < first && at <= phase) { first = at; hitBody = -1; catchRope = true; uncertain = false; }
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
        if (uncertain) break;  // the remaining time is solved conservatively below
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
        constexpr std::size_t kPairs = kBodyCount * (kBodyCount - 1) / 2;
        constexpr std::size_t kRows = 1 + kBodyCount * (1 + kMaxCorners) + kPairs * kMaxPatchEnds;
        std::array<Vec3, kBodyCount> normalTotal{}, gripTotal{}, atBody{}, floorNormal{}, floorGrip{}, atFloor{};
        std::array<double, kBodyCount> rollTotal{};
        struct PairImpulse {
            Vec3 normal{}, friction{}, weightedPoint{};
            double weight = 0.0, momentA = 0.0, momentB = 0.0;
        };
        std::array<std::array<PairImpulse, kBodyCount>, kBodyCount> pairImpulses{};
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
        const auto traceStage = [&](const char* stage, double time, double priorEnergy) {
            if (!traceTick) return;
            std::printf("ENERGY_STAGE tick %llu t %.9f stage %s E %.12g dE %.12g ball p(%.9g,%.9g) v(%.9g,%.9g) spin %.9g\\n",
                        static_cast<unsigned long long>(thisDebugTick), time, stage, localEnergy(),
                        localEnergy() - priorEnergy, state.position.x, state.position.y,
                        state.velocity.x, state.velocity.y, ball.spin);
            for (std::size_t i = 0; i < kBodyCount; ++i) {
                const Body& b = bodies_[i];
                if (b.count == 0) continue;
                std::printf("  ENERGY_BODY %zu p(%.9g,%.9g) v(%.9g,%.9g) omega %.9g KE %.12g PE %.12g\\n",
                            i,b.centre.x,b.centre.y,b.velocity.x,b.velocity.y,b.spin,
                            0.5*b.mass*dot(b.velocity,b.velocity)+0.5*b.moment*b.spin*b.spin,
                            b.mass*gravity*b.centre.y);
            }
        };
        const double initialEnergy = localEnergy();
        const Vec3 ballStartPosition = state.position, ballStartVelocity = state.velocity;
        const Vec3 shapeStartPosition = bodies_[kTriangle].centre, shapeStartVelocity = bodies_[kTriangle].velocity;
        const double shapeStartSpin = bodies_[kTriangle].spin, ballStartSpin = ball.spin;
        double projectionWork = 0.0;
        ball.touchCount = 0;  // the simultaneous solve below owns these contacts
        std::array<bool, kBodyCount> rebased{};
        std::array<double, kBodyCount> gravityTime{}, airMoment{}, bearingMoment{};
        std::array<Vec3, kBodyCount> airImpulse{};
        const auto bodyForces = [&](std::size_t i, double seconds) {
            Body& body = bodies_[i];
            if (bodyHeld(i)) return;
            playground_body_air::apply(body, seconds, [&](Vec3 at, Vec3 impulse, Vec3) {
                airImpulse[i] += impulse;
                if (traceTick) std::printf("FORCE_APPLY tick %llu body %zu source AIR at(%.9g,%.9g) J(%.9g,%.9g) dt %.9g\\n",
                                           static_cast<unsigned long long>(thisDebugTick), i, at.x, at.y,
                                           impulse.x, impulse.y, seconds);
                const Vec3 arm = at - body.centre;
                airMoment[i] += arm.x * impulse.y - arm.y * impulse.x;
            });
            if (!body.pivoted) body.velocity.y -= gravity * seconds;
            gravityTime[i] += seconds;
            if (body.pivoted && body.spin != 0.0) {
                const double limit = playground_detail::kAxleBearingFriction * body.mass * gravity *
                                     playground_detail::kAxleRadius * seconds;
                const double removed = std::copysign(std::min(std::abs(body.spin), limit / body.moment), body.spin);
                body.spin -= removed;
                bearingMoment[i] -= removed * body.moment;
            }
        };
        const auto rebaseBody = [&](std::size_t i, double time) {
            Body& body = bodies_[i];
            if (rebased[i] || bodyHeld(i)) return;
            const double spent = std::clamp(time / dt, 0.0, 1.0);
            if (spent < body.ropeEventAt) {
                const double preFraction = body.ropeEventAt > 0.0 ? spent / body.ropeEventAt : 0.0;
                body.velocity = body.arrivalVelocity +
                                (body.ropeEventVelocity - body.arrivalVelocity) * preFraction;
                body.spin = body.arrivalSpin + (body.ropeEventSpin - body.arrivalSpin) * preFraction;
            } else if (body.ropeEventAt < 1.0) {
                const double postFraction = (spent - body.ropeEventAt) / (1.0 - body.ropeEventAt);
                body.velocity = body.ropeEventVelocity +
                                (predictedVelocity[i] - body.ropeEventVelocity) * postFraction;
                body.spin = body.ropeEventSpin + (predictedSpin[i] - body.ropeEventSpin) * postFraction;
            } else {
                body.velocity = body.arrivalVelocity + (predictedVelocity[i] - body.arrivalVelocity) * spent;
                body.spin = body.arrivalSpin + (predictedSpin[i] - body.arrivalSpin) * spent;
            }
            for (std::size_t sample = 0; sample < predictedForces; ++sample) {
                ForceSample& force = forces_[sample];
                if (force.owner != i) continue;
                force.force = force.force * spent;
                force.moment *= spent;
            }
            if (body.frozen) {
                body.frozen = false;
                body.woken = true;
                body.restTicks = 0;
            }
            rebased[i] = true;
        };
        for (std::size_t i = 0; i < kBodyCount; ++i)
            if (stopped[i]) rebaseBody(i, elapsed);

        const auto constrain = [&](double step, double time, bool atStart) {
            std::array<pc::Body, kBodyCount + 1> motion{};
            motion[0] = {state.position, state.velocity, ball.spin, 1.0 / ball.mass, 1.0 / inertia};
            const double fraction = std::clamp(time / dt, 0.0, 1.0);
            for (std::size_t i = 0; i < kBodyCount; ++i) {
                const Body& body = bodies_[i];
                const bool fixed = bodyHeld(i);
                const Vec3 centre = stopped[i] ? body.centre
                                               : body.previousCentre + (body.centre - body.previousCentre) * fraction;
                Vec3 velocity = body.velocity;
                double spin = body.spin;
                if (!stopped[i]) {
                    if (fraction < body.ropeEventAt) {
                        const double preFraction = body.ropeEventAt > 0.0 ? fraction / body.ropeEventAt : 0.0;
                        velocity = body.arrivalVelocity +
                                   (body.ropeEventVelocity - body.arrivalVelocity) * preFraction;
                        spin = body.arrivalSpin + (body.ropeEventSpin - body.arrivalSpin) * preFraction;
                    } else if (body.ropeEventAt < 1.0) {
                        const double postFraction = (fraction - body.ropeEventAt) / (1.0 - body.ropeEventAt);
                        velocity = body.ropeEventVelocity + (body.velocity - body.ropeEventVelocity) * postFraction;
                        spin = body.ropeEventSpin + (body.spin - body.ropeEventSpin) * postFraction;
                    } else {
                        velocity = body.arrivalVelocity + (body.velocity - body.arrivalVelocity) * fraction;
                        spin = body.arrivalSpin + (body.spin - body.arrivalSpin) * fraction;
                    }
                }
                motion[i + 1] = {centre, fixed ? handSpeed_ : velocity, fixed ? 0.0 : spin,
                                 fixed || body.pivoted ? 0.0 : 1.0 / body.mass,
                                 fixed ? 0.0 : 1.0 / body.moment};
            }
            // Shapes not yet in this solve still follow the room's predicted
            // path. Once a contact reaches one, bring it into the same time slice
            // and solve its contacts with the rest of the island.
            std::array<std::array<Vec3, kMaxCorners>, kBodyCount> outlines{};
            for (std::size_t i = 0; i < kBodyCount; ++i)
                for (std::size_t j = 0; j < bodies_[i].count; ++j)
                    outlines[i][j] = stopped[i] ? bodies_[i].position[j]
                        : bodies_[i].previous[j] + (bodies_[i].position[j] - bodies_[i].previous[j]) * fraction;
            const auto activate = [&](std::size_t i) {
                if (stopped[i]) return;
                Body& body = bodies_[i];
                const double activationEnergyBefore = localEnergy();
                const Vec3 activationCentreBefore = body.centre;
                const Vec3 activationVelocityBefore = body.velocity;
                const double activationSpinBefore = body.spin;
                if (!bodyHeld(i)) {
                    const double when = body.pivoted ? fraction
                        : keepAboveFloor(i, body.previousCentre, body.previousAngle, body.centre, body.angle, fraction);
                    body.centre = body.pivoted ? body.pivotPoint
                        : body.previousCentre + (body.centre - body.previousCentre) * when;
                    body.angle = body.previousAngle + (body.angle - body.previousAngle) * when;
                    placeCorners(body);
                }
                stopped[i] = true;
                rebaseBody(i, time);
                // An arrival at the end of this substep has already followed the
                // old prediction through it; do not give it another half-step.
                if (atStart) bodyForces(i, 0.5 * step);
                const double activationEnergyAfter = localEnergy();
                if (traceTick || std::getenv("GTC_TRACE_ACTIVATE"))
                    std::printf("ACTIVATE t %.9f body %zu dE %+0.12g centre (%+.9g,%+.9g)->(%+.9g,%+.9g) v (%+.9g,%+.9g)->(%+.9g,%+.9g) omega %+.9g->%+.9g event %.9g time %.9g predicted (%+.9g,%+.9g) arrival (%+.9g,%+.9g)\\n",
                                time, i, activationEnergyAfter-activationEnergyBefore,
                                activationCentreBefore.x,activationCentreBefore.y,body.centre.x,body.centre.y,
                                activationVelocityBefore.x,activationVelocityBefore.y,body.velocity.x,body.velocity.y,
                                activationSpinBefore,body.spin,body.ropeEventAt,time,
                                predictedVelocity[i].x,predictedVelocity[i].y,
                                body.arrivalVelocity.x,body.arrivalVelocity.y);
                motion[i + 1].centre = body.centre;
                motion[i + 1].velocity = bodyHeld(i) ? handSpeed_ : body.velocity;
                motion[i + 1].spin = bodyHeld(i) ? 0.0 : body.spin;
                outlines[i] = body.position;
            };
            std::array<pc::Contact, kRows> rows{};
            std::size_t count = 0;
            for (std::size_t i = 0; i < kBodyCount; ++i) {
                Body& body = bodies_[i];
                if (body.count == 0) continue;
                OutlineGap gap = stopped[i]
                    ? outlineGap(state.position, body.position.data(), body.position.data(), body.count, 1.0)
                    : outlineGap(state.position, body.previous.data(), body.position.data(), body.count, fraction);
                double spare = gap.gap - ball.radius;
                Vec3 at = state.position - gap.normal * ball.radius;
                double closing = dot(pc::pointVelocity(motion[0], at) - pc::pointVelocity(motion[i + 1], at),
                                     gap.normal);
                if (spare > quantum + std::max(0.0, -closing * step)) continue;
                if (!stopped[i]) {
                    activate(i);
                    gap = outlineGap(state.position, body.position.data(), body.position.data(), body.count, 1.0);
                    spare = gap.gap - ball.radius;
                    at = state.position - gap.normal * ball.radius;
                    closing = dot(pc::pointVelocity(motion[0], at) - pc::pointVelocity(motion[i + 1], at), gap.normal);
                    if (spare > quantum + std::max(0.0, -closing * step)) continue;
                }
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

            // Extend the active island through arbitrary shape/shape contacts.
            // A body supporting the one touched by the ball must share this time
            // slice; treating its predicted pose as a fixed ghost lets the first
            // body fall straight through it. The band only gathers candidates;
            // each row below may spend no more than its actual clearance.
            double speedBound = length(state.velocity) + gravity * step;
            for (std::size_t i = 0; i < kBodyCount; ++i) {
                double radius = 0.0;
                for (std::size_t j = 0; j < bodies_[i].count; ++j)
                    radius = std::max(radius, length(bodies_[i].local[j]));
                speedBound += length(motion[i + 1].velocity) + std::abs(motion[i + 1].spin) * radius;
            }
            const double band = quantum + 2.0 * speedBound * step;
            for (std::size_t pass = 0; pass < kBodyCount; ++pass) {
                bool changed = false;
                for (std::size_t i = 0; i < kBodyCount; ++i) {
                    if (bodies_[i].count == 0) continue;
                    for (std::size_t j = i + 1; j < kBodyCount; ++j) {
                        if (bodies_[j].count == 0 || (!stopped[i] && !stopped[j])) continue;
                        pair_shape::PairManifold pair;
                        if (!pair_shape::collide(outlines[i].data(), bodies_[i].count,
                                                 outlines[j].data(), bodies_[j].count, band, pair)) continue;
                        if (!stopped[i]) { activate(i); changed = true; }
                        if (!stopped[j]) { activate(j); changed = true; }
                    }
                }
                if (!changed) break;
            }
            for (std::size_t i = 0; i < kBodyCount; ++i) {
                if (!stopped[i] || bodies_[i].count == 0) continue;
                for (std::size_t j = i + 1; j < kBodyCount; ++j) {
                    if (!stopped[j] || bodies_[j].count == 0) continue;
                    pair_shape::PairManifold pair;
                    if (!pair_shape::collide(outlines[i].data(), bodies_[i].count,
                                             outlines[j].data(), bodies_[j].count, band, pair)) continue;
                    for (std::size_t end = 0; end < pair.ends; ++end) {
                        const double spare = -pair.depth[end];
                        pc::Contact& row = rows[count++];
                        row.a = i + 1; row.b = j + 1;
                        row.point = pair.at[end]; row.normal = pair.normal;
                        row.target = spare > quantum ? -spare / step : 0.0;
                        row.staticFriction = std::min(bodies_[i].staticFriction, bodies_[j].staticFriction);
                        row.kineticFriction = std::min(bodies_[i].kineticFriction, bodies_[j].kineticFriction);
                    }
                }
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
            // Diagnostic ledger around the actual sequential-impulse solve. A
            // closed, stationary unilateral/frictional solve cannot increase
            // kinetic energy; report the individual contact rows whenever it does.
            const auto kinetic = [&]() {
                double value = 0.0;
                for (std::size_t i = 0; i < motion.size(); ++i) {
                    if (motion[i].inverseMass > 0.0)
                        value += 0.5 * dot(motion[i].velocity, motion[i].velocity) / motion[i].inverseMass;
                    if (motion[i].inverseMoment > 0.0)
                        value += 0.5 * motion[i].spin * motion[i].spin / motion[i].inverseMoment;
                }
                return value;
            };
            const double solveEnergyBefore = kinetic();
            const auto motionBefore = motion;
            std::array<double, kRows> oldNormal{}, oldTangent{}, oldRolling{};
            for (std::size_t r = 0; r < count; ++r) {
                oldNormal[r] = rows[r].normalImpulse;
                oldTangent[r] = rows[r].tangentImpulse;
                oldRolling[r] = rows[r].rollingImpulse;
            }
            pc::solve(motion, rows, count);
            const double solveEnergyAfter = kinetic();
            if (traceTick) {
                std::printf("SOLVE tick %llu t %.9f phase %s rows %zu dK %.12g\\n",
                            static_cast<unsigned long long>(thisDebugTick), time, atStart ? "begin" : "end",
                            count, solveEnergyAfter - solveEnergyBefore);
                for (std::size_t r = 0; r < count; ++r) {
                    const pc::Contact& c = rows[r];
                    const Vec3 va0 = pc::pointVelocity(motionBefore[c.a], c.point);
                    const Vec3 vb0 = c.b == pc::kWorld ? Vec3{} : pc::pointVelocity(motionBefore[c.b], c.point);
                    const Vec3 va1 = pc::pointVelocity(motion[c.a], c.point);
                    const Vec3 vb1 = c.b == pc::kWorld ? Vec3{} : pc::pointVelocity(motion[c.b], c.point);
                    const Vec3 j = c.normal * (c.normalImpulse - oldNormal[r]) +
                                   Vec3{c.normal.y, -c.normal.x, 0} * (c.tangentImpulse - oldTangent[r]);
                    std::printf("CONTACT row %zu A%zu B%zu point(%.9g,%.9g) n(%.9g,%.9g) gapTarget %.9g "
                                "vrelN %.9g->%.9g J(%.9g,%.9g) Favg(%.9g,%.9g) dJn %.9g dJt %.9g dJroll %.9g\\n",
                                r, c.a, c.b, c.point.x, c.point.y, c.normal.x, c.normal.y, c.target,
                                dot(va0-vb0,c.normal), dot(va1-vb1,c.normal), j.x, j.y, j.x/step, j.y/step,
                                c.normalImpulse-oldNormal[r], c.tangentImpulse-oldTangent[r],
                                c.rollingImpulse-oldRolling[r]);
                }
                for (std::size_t i = 0; i < motion.size(); ++i)
                    if (motion[i].inverseMass > 0.0 || motion[i].inverseMoment > 0.0)
                        std::printf("SOLID %zu v(%.9g,%.9g)->(%.9g,%.9g) omega %.9g->%.9g invM %.9g invI %.9g\\n",
                                    i, motionBefore[i].velocity.x, motionBefore[i].velocity.y,
                                    motion[i].velocity.x, motion[i].velocity.y, motionBefore[i].spin,
                                    motion[i].spin, motion[i].inverseMass, motion[i].inverseMoment);
            }
            if (std::getenv("GTC_TRACE_IMPULSE_ENERGY") && solveEnergyAfter - solveEnergyBefore > 1e-8) {
                std::printf("IMPULSE_SOLVE t %.9f dK %+.12g rows %zu\\n", time, solveEnergyAfter - solveEnergyBefore, count);
                for (std::size_t r = 0; r < count; ++r) {
                    const pc::Contact& c = rows[r];
                    const Vec3 va = pc::pointVelocity(motion[c.a], c.point);
                    const Vec3 vb = c.b == pc::kWorld ? Vec3{} : pc::pointVelocity(motion[c.b], c.point);
                    std::printf("  row %zu %zu->%zu n(%+.5f,%+.5f) target %+.7g jn %+.9g jt %+.9g vrelN %+.9g\\n",
                                r, c.a, c.b, c.normal.x, c.normal.y, c.target, c.normalImpulse,
                                c.tangentImpulse, dot(va - vb, c.normal));
                }
            }
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
                } else if (row.b == pc::kWorld) {
                    const std::size_t i = row.a - 1;
                    floorNormal[i] += normal; floorGrip[i] += grip; atFloor[i] = row.point;
                } else {
                    const std::size_t i = row.a - 1, k = row.b - 1;
                    PairImpulse& pair = pairImpulses[i][k];
                    pair.normal += normal;
                    pair.friction += grip;
                    const double weight = row.normalImpulse + std::abs(row.tangentImpulse);
                    pair.weightedPoint += row.point * weight;
                    pair.weight += weight;
                    const Vec3 total = normal + grip;
                    pair.momentA += pc::cross2(row.point - motion[row.a].centre, total);
                    pair.momentB -= pc::cross2(row.point - motion[row.b].centre, total);
                }
            }
            return ropeLoad;
        };

        while (elapsed < dt - 1e-12) {
            const double step = std::min(dt / kSubsteps, dt - elapsed);
            const double energyStart = localEnergy();
            state.velocity = damp(state.velocity, 0.5 * step) + Vec3{0, -0.5 * gravity * step, 0};
            for (std::size_t i = 0; i < kBodyCount; ++i)
                if (stopped[i]) bodyForces(i, 0.5 * step);
            const double energyAfterKick = localEnergy();
            traceStage("gravity+air first half-kick", elapsed, energyStart);
            const double firstLoad = constrain(step, elapsed, true);
            traceStage("first contact solve", elapsed, energyAfterKick);
            const double energyAfterFirstSolve = localEnergy();
            state.position += state.velocity * step;
            ball.angle += ball.spin * step;
            for (std::size_t i = 0; i < kBodyCount; ++i) {
                Body& body = bodies_[i];
                if (!stopped[i] || bodyHeld(i) || body.frozen) continue;
                if (!body.pivoted) body.centre += body.velocity * step;
                body.angle += body.spin * step;
                placeCorners(body);
            }
            const double energyAfterDrift = localEnergy();
            traceStage("position drift", elapsed + step, energyAfterFirstSolve);
            // Correct only geometrical drift, iterating because a rope and a
            // face can both be active. The velocity solve remains one-sided.
            const Vec3 beforeProjectionPosition = state.position;
            const double energyBeforeProjection = localEnergy();
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
                    const OutlineGap gap = stopped[i]
                        ? outlineGap(state.position, body.position.data(), body.position.data(), body.count, 1.0)
                        : outlineGap(state.position, body.previous.data(), body.position.data(), body.count,
                                     std::clamp((elapsed + step) / dt, 0.0, 1.0));
                    const double depth = ball.radius - gap.gap;
                    if (depth > 0.0) {
                        state.position += gap.normal * depth;
                        error = std::max(error, depth);
                    }
                }
                if (error < 1e-12) break;
            }
            projectionWork += ball.mass * gravity * (state.position.y - beforeProjectionY);
            if (traceTick && length(state.position - beforeProjectionPosition) > 0.0)
                std::printf("POSITION_CORRECTION tick %llu t %.9f ball p(%.9g,%.9g)->(%.9g,%.9g) dP(%.9g,%.9g) dE %.12g gravityWork %.12g\\n",
                            static_cast<unsigned long long>(thisDebugTick), elapsed+step,
                            beforeProjectionPosition.x,beforeProjectionPosition.y,state.position.x,state.position.y,
                            state.position.x-beforeProjectionPosition.x,state.position.y-beforeProjectionPosition.y,
                            localEnergy()-energyBeforeProjection,ball.mass*gravity*(state.position.y-beforeProjectionY));
            traceStage("geometric position correction", elapsed + step, energyBeforeProjection);
            const double energyBeforeSecondKick = localEnergy();
            state.velocity = damp(state.velocity + Vec3{0, -0.5 * gravity * step, 0}, 0.5 * step);
            for (std::size_t i = 0; i < kBodyCount; ++i)
                if (stopped[i]) bodyForces(i, 0.5 * step);
            const double energyBeforeSecondSolve = localEnergy();
            traceStage("gravity+air second half-kick", elapsed + step, energyBeforeSecondKick);
            finalRopeLoad = (firstLoad + constrain(step, elapsed + step, false)) / step;
            traceStage("second contact solve", elapsed + step, energyBeforeSecondSolve);
            elapsed += step;
            if (std::getenv("GTC_TRACE_SUBSTEP_ENERGY")) {
                const double energyEnd = localEnergy();
                if (energyEnd - energyStart > 1e-5 || energyAfterFirstSolve - energyStart > 1e-5 ||
                    energyAfterDrift - energyAfterFirstSolve > 1e-5)
                    std::printf("SUBSTEP t %.6f dE %+0.8f solve %+0.8f drift %+0.8f end %+0.8f ball v (%+.4f,%+.4f) shape v (%+.4f,%+.4f)\\n",
                                elapsed, energyEnd - energyStart, energyAfterFirstSolve - energyStart,
                                energyAfterDrift - energyAfterFirstSolve, energyEnd - energyStart,
                                state.velocity.x, state.velocity.y, bodies_[kTriangle].velocity.x,
                                bodies_[kTriangle].velocity.y);
            }
        }
        static int constraintTraces = 0;
        if (std::getenv("GTC_TRACE_CONSTRAINTS") && (!std::getenv("GTC_TRACE_HELD") || bodyHeld(kTriangle)) &&
            localEnergy() - initialEnergy > 1e-5 && constraintTraces++ < 25)
            std::printf("CONSTRAINT energy %+.8f J projection %+.8f J "
                        "ball p (%+.6f,%+.6f)->(%+.6f,%+.6f) v (%+.6f,%+.6f)->(%+.6f,%+.6f) spin %+.5f->%+.5f "
                        "shape p (%+.6f,%+.6f)->(%+.6f,%+.6f) v (%+.6f,%+.6f)->(%+.6f,%+.6f) spin %+.5f->%+.5f held %d\n",
                        localEnergy() - initialEnergy, projectionWork, ballStartPosition.x, ballStartPosition.y,
                        state.position.x, state.position.y, ballStartVelocity.x, ballStartVelocity.y,
                        state.velocity.x, state.velocity.y, ballStartSpin, ball.spin,
                        shapeStartPosition.x, shapeStartPosition.y, bodies_[kTriangle].centre.x,
                        bodies_[kTriangle].centre.y, shapeStartVelocity.x, shapeStartVelocity.y,
                        bodies_[kTriangle].velocity.x, bodies_[kTriangle].velocity.y, shapeStartSpin,
                        bodies_[kTriangle].spin, int(bodyHeld(kTriangle)));
        for (std::size_t i = 0; i < kBodyCount; ++i) {
            if (gravityTime[i] > 0.0)
                addForce(bodies_[i].centre, Vec3{0, -bodies_[i].mass * gravity * gravityTime[i] / dt, 0},
                         ForceKind::Gravity, nullptr, i);
            if (length(airImpulse[i]) > 1e-12)
                addForce(bodies_[i].centre, airImpulse[i] / dt, ForceKind::Drag, nullptr, i);
            if (std::abs(airMoment[i]) > 1e-12)
                addCouple(bodies_[i].centre, airMoment[i] / dt, {}, ForceKind::Drag, i);
            if (std::abs(bearingMoment[i]) > 1e-12)
                addCouple(bodies_[i].pivotPoint, bearingMoment[i] / dt, {}, ForceKind::Rolling, i);
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
        // Each shape pair is an equal-and-opposite impulse. Keep its normal
        // and friction separate in the ledger, and preserve the moment lost
        // by drawing the distributed substep impulses at one averaged point.
        for (std::size_t i = 0; i < kBodyCount; ++i) {
            for (std::size_t j = i + 1; j < kBodyCount; ++j) {
                const PairImpulse& pair = pairImpulses[i][j];
                if (!(pair.weight > 0.0)) continue;
                const Vec3 at = pair.weightedPoint / pair.weight;
                const Vec3 total = pair.normal + pair.friction;
                if (length(pair.normal) > 1e-12) {
                    recordImpulse(at, pair.normal, ForceKind::Pair, i);
                    recordImpulse(at, pair.normal * -1.0, ForceKind::Pair, j);
                }
                if (length(pair.friction) > 1e-12) {
                    recordImpulse(at, pair.friction, ForceKind::Friction, i);
                    recordImpulse(at, pair.friction * -1.0, ForceKind::Friction, j);
                }
                const double residualA = pair.momentA - pc::cross2(at - bodies_[i].centre, total);
                const double residualB = pair.momentB + pc::cross2(at - bodies_[j].centre, total);
                if (std::abs(residualA) > 1e-12) addCouple(at, residualA / dt, {}, ForceKind::Pair, i);
                if (std::abs(residualB) > 1e-12) addCouple(at, residualB / dt, {}, ForceKind::Pair, j);
            }
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
