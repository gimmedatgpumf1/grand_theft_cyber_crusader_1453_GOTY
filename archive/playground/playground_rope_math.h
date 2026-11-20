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
