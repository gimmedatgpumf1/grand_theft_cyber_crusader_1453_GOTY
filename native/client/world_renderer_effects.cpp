#include "client/world_renderer_draw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gtc::native::world_draw {

void drawPaths(Rasterizer& rasterizer, const Camera& camera, const ClientState& state, std::uint32_t localId) {
    const Character* local = state.character(localId);
    if (!local || local->path.empty()) return;
    Material marker;
    marker.color = colorHex("#8ce6c8");
    marker.specular = 0.2;
    gtc::Vec2 previous = local->position;
    for (gtc::Vec2 point : local->path) {
        const double length = distance(previous, point);
        const int steps = std::max(1, static_cast<int>(length / 0.28));
        for (int i = 1; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            const gtc::Vec2 at{static_cast<float>(previous.x + (point.x - previous.x) * t),
                               static_cast<float>(previous.y + (point.y - previous.y) * t)};
            drawEllipsoid(rasterizer, camera, Vec3{at.x, 0.025, at.y}, Vec3{0.045, 0.012, 0.045}, marker, 8, 4);
        }
        previous = point;
    }
}

void drawWater(Rasterizer& rasterizer, const Camera& camera, const ClientWorld& world,
               const ClientState& state, double time) {
    constexpr double pi = 3.141592653589793;
    const Vec3 center{world.toilet.x, kToiletWaterHeight, world.toilet.y + kToiletBowlOffset};
    auto heightAt = [&](double x, double z) {
        double height = 0.0018 * std::sin(x * 31.0 + time * 3.4) * std::cos(z * 37.0 - time * 2.7);
        for (const WaterRipple& ripple : state.waterRipples) {
            const double d = std::hypot(x + center.x - ripple.position.x, z + center.z - ripple.position.y);
            height += std::sin(d * 68.0 - ripple.age * 19.0) * std::exp(-d * 10.0 - ripple.age * 2.4) *
                0.009 * ripple.strength;
        }
        return height;
    };
    auto point = [&](double radius, double angle) {
        const double x = std::cos(angle) * kToiletWater.x * radius;
        const double z = std::sin(angle) * kToiletWater.y * radius;
        return center + Vec3{x, heightAt(x, z), z};
    };
    Material water;
    water.color = colorHex("#55a894");
    water.specular = 0.82;
    water.opacity = 0.38;
    for (int ring = 0; ring < 5; ++ring) {
        for (int i = 0; i < 36; ++i) {
            const double a = i * 2.0 * pi / 36.0, b = (i + 1) * 2.0 * pi / 36.0;
            drawQuad(rasterizer, camera, {point(ring / 5.0, a), point((ring + 1) / 5.0, a),
                point((ring + 1) / 5.0, b), point(ring / 5.0, b)}, {0, 1, 0}, water);
        }
    }
    int splashed = 0;
    for (const WaterRipple& ripple : state.waterRipples) {
        const double fade = std::clamp(1.0 - ripple.age / 1.8, 0.0, 1.0);
        Material foam;
        foam.color = colorHex("#b5e4cf");
        foam.specular = 0.5;
        foam.opacity = 0.62 * fade;
        const double radius = 0.012 + ripple.age * 0.095;
        for (int i = 0; i < 28; ++i) {
            const double a = i * 2.0 * pi / 28.0, b = (i + 1) * 2.0 * pi / 28.0;
            auto edge = [&](double r, double angle) {
                return Vec3{ripple.position.x + std::cos(angle) * r,
                    kToiletWaterHeight + 0.005, ripple.position.y + std::sin(angle) * r};
            };
            const Vec3 a0 = edge(radius, a), a1 = edge(radius + 0.0035, a);
            const Vec3 b0 = edge(radius, b), b1 = edge(radius + 0.0035, b);
            auto inside = [&](Vec3 p) {
                const double x = (p.x - center.x) / kToiletWater.x, z = (p.z - center.z) / kToiletWater.y;
                return x * x + z * z < 0.98;
            };
            if (inside(a0) && inside(b0)) drawQuad(rasterizer, camera, {a0, a1, b1, b0}, {0, 1, 0}, foam);
        }
        if (ripple.age < 0.48 && splashed++ < 20) {
            Material splash = water;
            splash.opacity = 0.7 * (1.0 - ripple.age / 0.48);
            for (int j = 0; j < 4; ++j) {
                const double angle = j * 2.39996323 + ripple.id;
                const double travel = ripple.age * 0.12;
                const double lift = std::sin(ripple.age / 0.48 * pi) * 0.075 * ripple.strength;
                drawEllipsoid(rasterizer, camera, {ripple.position.x + std::cos(angle) * travel,
                    kToiletWaterHeight + 0.012 + lift, ripple.position.y + std::sin(angle) * travel},
                    {0.005, 0.014, 0.005}, splash, 6, 3);
            }
        }
    }
}

void drawPuddles(Rasterizer& rasterizer, const Camera& camera, const ClientState& state) {
    Material puddle;
    puddle.kind = Material::Kind::Puddle;
    puddle.color = colorHex("#414713");
    puddle.specular = 0.18;
    puddle.opacity = 0.54;
    const auto center = [](const Puddle& value) { return Vec3{value.position.x, value.height, value.position.y}; };
    const auto normal = [](const Puddle& value) {
        return value.vertical ? Vec3{value.normal.x, 0, value.normal.y} : Vec3{0, 1, 0};
    };
    std::vector<const Puddle*> chain;
    for (const Puddle& value : state.puddles) chain.push_back(&value);
    std::sort(chain.begin(), chain.end(), [](const Puddle* a, const Puddle* b) { return a->id < b->id; });
    std::map<const Puddle*, int> connections;
    for (std::size_t i = 1; i < chain.size(); ++i) {
        const Puddle& a = *chain[i - 1];
        const Puddle& b = *chain[i];
        const Vec3 delta = center(b) - center(a);
        const double span = length(delta);
        if (a.vertical != b.vertical || length(normal(a) - normal(b)) > 0.1 ||
            std::abs(dot(delta, normal(a))) > 0.02 || span < 0.02 || span > 0.55) continue;
        const Vec3 direction = delta / span;
        const Vec3 sideA = normalized(cross(normal(a), direction)) * a.size;
        const Vec3 sideB = normalized(cross(normal(b), direction)) * b.size;
        const Vec3 ca = center(a), cb = center(b);
        drawQuad(rasterizer, camera, {ca + sideA, cb + sideB, cb - sideB, ca - sideA}, normal(a), puddle);
        ++connections[&a];
        ++connections[&b];
    }
    for (const Puddle& puddleValue : state.puddles) {
        if (connections[&puddleValue] > 1) continue; // Interior discs are replaced by the continuous ribbon.
        const Vec3 centerPoint = center(puddleValue);
        const Vec2 tangent{-puddleValue.normal.y, puddleValue.normal.x};
        const Vec3 axisA = puddleValue.vertical ? Vec3{tangent.x * puddleValue.size * 0.72, 0,
            tangent.y * puddleValue.size * 0.72} : Vec3{puddleValue.size, 0, 0};
        const Vec3 axisB = puddleValue.vertical ? Vec3{0, puddleValue.size, 0} : Vec3{0, 0, puddleValue.size * 0.72};
        for (int i = 0; i < 10; ++i) {
            const double a = i * 2.0 * 3.141592653589793 / 10.0, b = (i + 1) * 2.0 * 3.141592653589793 / 10.0;
            const Vec3 first = centerPoint + axisA * std::cos(a) + axisB * std::sin(a);
            const Vec3 second = centerPoint + axisA * std::cos(b) + axisB * std::sin(b);
            rasterizer.triangle({cameraVertex(camera, centerPoint, normal(puddleValue), centerPoint),
                cameraVertex(camera, first, normal(puddleValue), first),
                cameraVertex(camera, second, normal(puddleValue), second)}, puddle);
        }
    }
}

void drawProjectiles(Rasterizer& rasterizer, const Camera& camera, const ClientState& state,
                     const std::unordered_map<std::uint32_t, HumanRenderCache>& humanCaches) {
    Material piss, bullet;
    piss.color = colorHex("#f2ce24");
    piss.unlit = true;
    piss.opacity = 0.88;
    bullet.color = colorHex("#fff0a0");
    bullet.unlit = true;
    const auto point = [](const Projectile& value) { return Vec3{value.position.x, value.height, value.position.y}; };
    auto link = [&](Vec3 a, Vec3 b, double phase, double maxSpan = 0.85) {
        const Vec3 delta = b - a;
        const double span = length(delta);
        if (span < 0.008 || span > maxSpan) return;
        const Vec3 direction = delta / span;
        Vec3 side = cross(direction, camera.position - (a + b) * 0.5);
        if (length(side) < 0.001) side = cross(direction, Vec3{0, 1, 0});
        if (length(side) < 0.001) side = {1, 0, 0};
        side = normalized(side);
        const Vec3 normal = normalized(cross(side, direction));
        Vec3 previous = a;
        for (int segment = 1; segment <= 3; ++segment) {
            const double t = segment / 3.0;
            Vec3 next = a + delta * t;
            if (segment < 3) next = next + side * (0.0016 * std::sin(phase + t * 13.0) +
                0.0007 * std::sin(phase * 1.7 + t * 29.0));
            drawQuad(rasterizer, camera, {previous + side * 0.0038, next + side * 0.0038,
                next - side * 0.0038, previous - side * 0.0038}, normal, piss);
            previous = next;
        }
    };
    std::vector<const Projectile*> stream;
    for (const Projectile& projectile : state.projectiles)
        if (projectile.kind == ProjectileKind::Piss) stream.push_back(&projectile);
    std::sort(stream.begin(), stream.end(), [](const Projectile* a, const Projectile* b) {
        return a->owner != b->owner ? a->owner < b->owner : a->id < b->id;
    });
    for (std::size_t index = 0; index < stream.size(); ++index) {
        const Projectile& oldest = *stream[index];
        if (oldest.toilet && (index == 0 || stream[index - 1]->owner != oldest.owner))
            link(point(oldest), {oldest.target.x, oldest.targetHeight, oldest.target.y}, oldest.owner * 0.713, 25.0);
        if (index == 0) continue;
        const Projectile& older = *stream[index - 1];
        const Projectile& newer = *stream[index];
        const double ageGap = older.age - newer.age;
        if (older.owner == newer.owner && ageGap >= -0.001 && ageGap <= 0.075)
            link(point(older), point(newer), older.owner * 0.713 + state.tick * 0.17);
    }
    for (std::size_t index = 0; index < stream.size(); ++index) {
        const Projectile& newest = *stream[index];
        const Character* owner = state.character(newest.owner);
        if ((index + 1 < stream.size() && stream[index + 1]->owner == newest.owner) || !owner ||
            !humanStreamAttached(*owner) || newest.age > 0.075) continue;
        const float pitch = std::atan2(newest.verticalVelocity + kProjectileGravity * newest.age, length(newest.velocity));
        const auto cached = humanCaches.find(newest.owner);
        const Vec3 exit = humanStreamOrigin(*owner, cached == humanCaches.end() ? nullptr : &cached->second, pitch);
        // Start inside the organ so the ribbon emerges from the opening itself.
        link(exit - normalized(point(newest) - exit) * 0.006, point(newest), owner->id * 0.713 + state.tick * 0.17);
    }
    for (const Projectile& projectile : state.projectiles) {
        if (projectile.kind != ProjectileKind::Bullet) continue;
        const Vec3 center = point(projectile);
        Vec3 direction{projectile.velocity.x, projectile.verticalVelocity, projectile.velocity.y};
        if (length(direction) < 0.001) direction = {0, 1, 0};
        direction = normalized(direction);
        Vec3 side = cross(direction, Vec3{0, 1, 0});
        if (length(side) < 0.001) side = {1, 0, 0};
        side = normalized(side);
        const Vec3 up = normalized(cross(side, direction));
        const Vec3 front = center + direction * 0.12, back = center - direction * 0.12;
        const Vec3 ring[4] = {center + side * 0.018, center + up * 0.018, center - side * 0.018, center - up * 0.018};
        for (int i = 0; i < 4; ++i) {
            const Vec3 a = ring[i], b = ring[(i + 1) % 4];
            const Vec3 normalA = normalized(cross(a - front, b - front));
            const Vec3 normalB = normalized(cross(b - back, a - back));
            rasterizer.triangle({cameraVertex(camera, front, normalA, front), cameraVertex(camera, a, normalA, a),
                                 cameraVertex(camera, b, normalA, b)}, bullet);
            rasterizer.triangle({cameraVertex(camera, back, normalB, back), cameraVertex(camera, b, normalB, b),
                                 cameraVertex(camera, a, normalB, a)}, bullet);
        }
    }
}

}  // namespace gtc::native::world_draw
