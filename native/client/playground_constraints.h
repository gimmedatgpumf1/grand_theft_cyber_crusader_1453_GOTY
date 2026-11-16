#pragma once

// Planar rigid-body velocity constraints. The normal points from B towards A;
// positive normal impulse separates them. A world endpoint has no mass or spin,
// so the same constraint represents a fixed surface or a rope anchor.
#include "native.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace gtc::native::playground_constraints {

constexpr std::size_t kWorld = std::numeric_limits<std::size_t>::max();

struct Body {
    Vec3 centre{}, velocity{};
    double spin = 0.0;
    double inverseMass = 0.0, inverseMoment = 0.0;
};

struct Contact {
    std::size_t a = 0, b = kWorld;
    Vec3 point{}, normal{};
    // Minimum permitted relative normal velocity. For a resting contact this is
    // zero; a positive value is an impact's restitution target. A negative
    // value can represent the distance still available during a time step.
    double target = 0.0;
    double staticFriction = 0.0, kineticFriction = 0.0;
    // A rolling-resistance couple, bounded by Crr * normal impulse * radius.
    double rollingFriction = 0.0, rollingRadius = 0.0;
    double normalImpulse = 0.0, tangentImpulse = 0.0, rollingImpulse = 0.0;
};

inline double cross2(Vec3 a, Vec3 b) { return a.x * b.y - a.y * b.x; }

inline Vec3 pointVelocity(const Body& body, Vec3 point) {
    const Vec3 arm = point - body.centre;
    return body.velocity + Vec3{-body.spin * arm.y, body.spin * arm.x, 0.0};
}

inline double compliance(const Body& body, Vec3 point, Vec3 direction) {
    const double lever = cross2(point - body.centre, direction);
    return body.inverseMass + lever * lever * body.inverseMoment;
}

inline void apply(Body& body, Vec3 point, Vec3 impulse) {
    body.velocity += impulse * body.inverseMass;
    body.spin += cross2(point - body.centre, impulse) * body.inverseMoment;
}

// Projected sequential impulses solve all rows together. Accumulated impulses
// may be reduced on later passes, but a normal impulse can never become tensile.
// Friction is bounded by the current normal impulse and acts at the same point.
template <std::size_t Bodies, std::size_t Contacts>
void solve(std::array<Body, Bodies>& bodies, std::array<Contact, Contacts>& contacts,
           std::size_t count, int iterations = 256) {
    count = std::min(count, Contacts);
    for (int pass = 0; pass < iterations; ++pass) {
        double largest = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            Contact& contact = contacts[i];
            if (contact.a >= Bodies || (contact.b != kWorld && contact.b >= Bodies)) continue;
            Body& a = bodies[contact.a];
            Body* b = contact.b == kWorld ? nullptr : &bodies[contact.b];
            const auto relative = [&]() {
                return pointVelocity(a, contact.point) - (b ? pointVelocity(*b, contact.point) : Vec3{});
            };
            const auto applyPair = [&](Vec3 impulse) {
                apply(a, contact.point, impulse);
                if (b) apply(*b, contact.point, impulse * -1.0);
            };
            const double normalGive = compliance(a, contact.point, contact.normal) +
                                      (b ? compliance(*b, contact.point, contact.normal) : 0.0);
            if (!(normalGive > 1e-15)) continue;
            const double normalSpeed = dot(relative(), contact.normal);
            const double nextNormal = std::max(0.0, contact.normalImpulse +
                                                        (contact.target - normalSpeed) / normalGive);
            const double normalChange = nextNormal - contact.normalImpulse;
            contact.normalImpulse = nextNormal;
            applyPair(contact.normal * normalChange);
            largest = std::max(largest, std::abs(normalChange));

            const Vec3 tangent{contact.normal.y, -contact.normal.x, 0.0};
            const double tangentGive = compliance(a, contact.point, tangent) +
                                       (b ? compliance(*b, contact.point, tangent) : 0.0);
            if (!(tangentGive > 1e-15)) continue;
            const double slip = dot(relative(), tangent);
            double nextTangent = contact.tangentImpulse - slip / tangentGive;
            const double staticLimit = contact.staticFriction * nextNormal;
            if (std::abs(nextTangent) > staticLimit) {
                const double kineticLimit = contact.kineticFriction * nextNormal;
                nextTangent = std::clamp(nextTangent, -kineticLimit, kineticLimit);
            }
            const double tangentChange = nextTangent - contact.tangentImpulse;
            contact.tangentImpulse = nextTangent;
            applyPair(tangent * tangentChange);
            largest = std::max(largest, std::abs(tangentChange));

            const double angularGive = a.inverseMoment + (b ? b->inverseMoment : 0.0);
            if (contact.rollingFriction > 0.0 && contact.rollingRadius > 0.0 && angularGive > 1e-15) {
                const double relativeSpin = a.spin - (b ? b->spin : 0.0);
                const double limit = contact.rollingFriction * contact.rollingRadius * nextNormal;
                const double nextRolling = std::clamp(contact.rollingImpulse - relativeSpin / angularGive,
                                                      -limit, limit);
                const double change = nextRolling - contact.rollingImpulse;
                contact.rollingImpulse = nextRolling;
                a.spin += change * a.inverseMoment;
                if (b) b->spin -= change * b->inverseMoment;
                largest = std::max(largest, std::abs(change));
            }
        }
        if (largest < 1e-12) break;
    }
}

}  // namespace gtc::native::playground_constraints
