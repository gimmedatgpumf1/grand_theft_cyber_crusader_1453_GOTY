// Included inside the arrival checks' anonymous namespace so these use the same
// reporting as the rest of the playground tests.
void contactConstraintChecks() {
    namespace pc = gtc::native::playground_constraints;
    const auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    {
        std::array<pc::Body, 2> bodies{};
        bodies[0] = {{-1, 0, 0}, {4, 0, 0}, 0, 1.0 / 0.62, 0};
        bodies[1] = {{1, 0, 0}, {}, 0, 1.0 / 3.0, 0};
        std::array<pc::Contact, 1> contacts{};
        contacts[0].a = 0;
        contacts[0].b = 1;
        contacts[0].normal = {-1, 0, 0};
        contacts[0].target = 3.0;  // e=0.75 at a 4 m/s closing speed
        pc::solve(bodies, contacts, 1);
        const double momentum = 0.62 * bodies[0].velocity.x + 3.0 * bodies[1].velocity.x;
        const double relative = dot(bodies[0].velocity - bodies[1].velocity, contacts[0].normal);
        const double energy = 0.31 * dot(bodies[0].velocity, bodies[0].velocity) +
                              1.5 * dot(bodies[1].velocity, bodies[1].velocity);
        char detail[160];
        std::snprintf(detail, sizeof detail, "relative %.6f m/s, momentum %.6f N s, energy %.6f J",
                      relative, momentum, energy);
        check(near(relative, 3.0) && near(momentum, 2.48) && energy <= 0.31 * 16.0,
              "a shared impulse preserves momentum and impact restitution", detail);
    }
    {
        const double diagonal = std::sqrt(0.5);
        const auto corner = [diagonal](bool reverse) {
            std::array<pc::Body, 1> bodies{};
            bodies[0] = {{}, {-1, -0.2, 0.7}, 0, 1, 0};
            std::array<pc::Contact, 2> contacts{};
            contacts[0].normal = {0, 1, 0};
            contacts[1].normal = {diagonal, -diagonal, 0};
            if (reverse) std::swap(contacts[0], contacts[1]);
            pc::solve(bodies, contacts, contacts.size(), 128);
            return bodies[0].velocity;
        };
        const Vec3 forward = corner(false), reverse = corner(true);
        check(length(forward - Vec3{0, 0, 0.7}) < 1e-9 && length(reverse - forward) < 1e-9,
              "simultaneous unilateral contacts agree in either order", "");
    }
    {
        const auto rope = [](double velocity) {
            std::array<pc::Body, 1> bodies{};
            bodies[0] = {{}, {0, velocity, 0}, 0, 1, 0};
            std::array<pc::Contact, 1> contacts{};
            contacts[0].normal = {0, 1, 0};
            pc::solve(bodies, contacts, 1);
            return std::array<double, 2>{bodies[0].velocity.y, contacts[0].normalImpulse};
        };
        const auto outward = rope(-2), inward = rope(2);
        check(near(outward[0], 0) && near(outward[1], 2) && near(inward[0], 2) && near(inward[1], 0),
              "a unilateral rope constraint pulls outward motion but never pushes", "");
    }
    {
        std::array<pc::Body, 1> bodies{};
        bodies[0] = {{0, 1, 0}, {1, -1, 0}, 0, 1, 2.5};  // solid sphere, radius and mass one
        std::array<pc::Contact, 1> contacts{};
        contacts[0].normal = {0, 1, 0};
        contacts[0].staticFriction = 0.7;
        contacts[0].kineticFriction = 0.55;
        pc::solve(bodies, contacts, 1);
        const Vec3 contactVelocity = pc::pointVelocity(bodies[0], contacts[0].point);
        const double energy = 0.5 * dot(bodies[0].velocity, bodies[0].velocity) +
                              0.2 * bodies[0].spin * bodies[0].spin;
        check(std::abs(contactVelocity.x) < 1e-9 && near(bodies[0].velocity.y, 0) &&
                  std::abs(contacts[0].tangentImpulse) <= 0.7 * contacts[0].normalImpulse && energy <= 1.0,
              "friction respects its normal load and produces rolling", "");
    }
}
