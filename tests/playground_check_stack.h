// A ball and a stack must not be solved as if the supporting body disappeared
// when the ball's sustained-contact step takes over.
void ropeAgainstSupportedStack(double angle, double supportOffset, bool supportAsleep, int ticks) {
    PlaygroundRoom room;
    room.reset();
    settle(room, 200);
    auto& other = room.ball(PlaygroundRoom::kThrowBall);
    other.position = other.previous = other.shown = {-3, other.radius, 0};
    other.velocity = {};
    other.spin = 0;
    other.frozen = true;
    // Park the unrelated dot on its own floor support. In mid-air it wakes and
    // loses energy as it falls, masking energy gained by the stack under test.
    room.point_ = room.previous_ = {-4, 0, 0};
    room.velocity_ = {};
    room.resting_ = true;
    room.restingBody_ = -1;
    room.restingOnBall_ = false;
    room.pointFrozen_ = true;

    auto& plank = room.bodies_[PlaygroundRoom::kSeesaw];
    plank.centre = plank.previousCentre = plank.pivotPoint = {-20, 3, 0};
    plank.velocity = {};
    plank.spin = 0;
    plank.frozen = true;
    room.placeCorners(plank);
    plank.previous = plank.shown = plank.position;

    auto& ball = room.ball(PlaygroundRoom::kRopeBall);
    ball.position = ball.previous = ball.shown = room.ropeAnchor() +
                    Vec3{std::sin(angle), -std::cos(angle), 0} * room.ropeLength();
    ball.velocity = {};
    ball.spin = 0;
    ball.frozen = false;
    ball.touching = false;
    room.rope_.taut = true;

    auto& triangle = room.bodies_[PlaygroundRoom::kTriangle];
    triangle.centre = triangle.previousCentre = {ball.position.x + ball.radius + 0.7, 1.4, 0};
    triangle.angle = triangle.previousAngle = 0;
    triangle.velocity = {};
    triangle.spin = 0;
    triangle.frozen = false;
    room.placeCorners(triangle);
    triangle.previous = triangle.shown = triangle.position;

    auto& cube = room.bodies_[PlaygroundRoom::kCube];
    cube.centre = cube.previousCentre = {triangle.centre.x + supportOffset, 0.35, 0};
    cube.angle = cube.previousAngle = 0;
    cube.velocity = {};
    cube.spin = 0;
    cube.frozen = supportAsleep;
    room.placeCorners(cube);
    cube.previous = cube.shown = cube.position;

    const auto islandEnergy = [&]() {
        const auto bodyEnergy = [](const auto& body) {
            return body.mass * ball_shape::kGravity * body.centre.y + 0.5 * body.mass * dot(body.velocity, body.velocity) +
                   0.5 * body.moment * body.spin * body.spin;
        };
        const double inertia = 0.4 * ball.mass * ball.radius * ball.radius;
        return bodyEnergy(triangle) + bodyEnergy(cube) + ball.mass * ball_shape::kGravity * ball.position.y +
               0.5 * ball.mass * dot(ball.velocity, ball.velocity) + 0.5 * inertia * ball.spin * ball.spin;
    };
    double overlap = 0, gain = 0, islandGain = 0, stretch = 0, meeting = 0;
    double momentumError = 0, pairError = 0;
    int worstTick = 0, worstIslandTick = 0, overlapTick = 0, momentumTick = 0;
    for (int tick = 1; tick <= ticks; ++tick) {
        const double energy = room.totalEnergy(), localBefore = islandEnergy();
        const Vec3 triangleBefore = triangle.velocity, cubeBefore = cube.velocity;
        if (std::getenv("GTC_TRACE_STACK_LEDGER") && angle == -1.0 && supportOffset == -0.15 &&
            !supportAsleep && ticks == 200 && tick >= 83 && tick <= 85) {
            PlaygroundRoom probe = room;
            for (auto& bob : probe.balls_) { bob.arrival = bob.velocity; bob.blows = {}; }
            for (auto& shape : probe.bodies_) shape.woken = false;
            probe.clearForces();
            const auto stage = [&](const char* label) {
                const auto& b = probe.ball(PlaygroundRoom::kRopeBall);
                const auto& t = probe.bodies_[PlaygroundRoom::kTriangle];
                std::printf("  STAGE %d %s E %.9f ball p (%+.6f,%+.6f) v (%+.6f,%+.6f) spin %+.6f "
                            "shape p (%+.6f,%+.6f) v (%+.6f,%+.6f) angle %+.6f spin %+.6f\n",
                            tick, label, probe.totalEnergy(), b.position.x, b.position.y, b.velocity.x, b.velocity.y,
                            b.spin, t.centre.x, t.centre.y, t.velocity.x, t.velocity.y, t.angle, t.spin);
            };
            stage("start");
            probe.tickBodies(); stage("bodies");
            probe.tickBalls(); stage("balls");
            for (auto& bob : probe.balls_) probe.separateBall(bob);
            stage("separate");
            probe.settleBodies(); probe.freezeCheck(); stage("settle");
        }
        room.advance(1.0 / 20.0);
        pair_shape::PairManifold patch;
        if (pair_shape::collide(triangle.position.data(), triangle.count, cube.position.data(), cube.count, 0, patch) &&
            patch.overlap > overlap) { overlap = patch.overlap; overlapTick = tick; }
        const double change = room.totalEnergy() - energy, localChange = islandEnergy() - localBefore;
        if (std::getenv("GTC_TRACE_STACK") && (tick <= 30 || patch.overlap > 1e-6 || localChange > 0.001)) {
            std::printf("stack %d overlap %.9f dE %+.8f island %+.8f ball (%+.4f,%+.4f) v (%+.4f,%+.4f) "
                        "triangle (%+.4f,%+.4f) v (%+.4f,%+.4f) spin %+.4f "
                        "cube (%+.4f,%+.4f) v (%+.4f,%+.4f) spin %+.4f\n",
                        tick, patch.overlap, change, localChange, ball.position.x, ball.position.y,
                        ball.velocity.x, ball.velocity.y, triangle.centre.x, triangle.centre.y,
                        triangle.velocity.x, triangle.velocity.y, triangle.spin, cube.centre.x, cube.centre.y,
                        cube.velocity.x, cube.velocity.y, cube.spin);
        }
        if (change > gain) { gain = change; worstTick = tick; }
        if (localChange > islandGain) { islandGain = localChange; worstIslandTick = tick; }
        stretch = std::max(stretch, room.ropeBallDistance() - room.ropeLength());
        Vec3 triangleImpulse{}, cubeImpulse{}, pairImpulse{};
        for (std::size_t i = 0; i < room.forceCount(); ++i) {
            const auto& force = room.forces()[i];
            if (force.couple) continue;
            if (force.owner == PlaygroundRoom::kTriangle) {
                triangleImpulse += force.force / 20.0;
                if (force.kind == PlaygroundRoom::ForceKind::Ball)
                    meeting = std::max(meeting, length(force.force) / 20.0);
            }
            if (force.owner == PlaygroundRoom::kCube) cubeImpulse += force.force / 20.0;
            if ((force.owner == PlaygroundRoom::kTriangle || force.owner == PlaygroundRoom::kCube) &&
                force.kind == PlaygroundRoom::ForceKind::Pair) pairImpulse += force.force / 20.0;
        }
        const double error = std::max(length((triangle.velocity - triangleBefore) * triangle.mass - triangleImpulse),
                                      length((cube.velocity - cubeBefore) * cube.mass - cubeImpulse));
        if (error > momentumError) { momentumError = error; momentumTick = tick; }
        pairError = std::max(pairError, length(pairImpulse));
        if (std::getenv("GTC_TRACE_STACK_LEDGER") && angle == -1.0 && !supportAsleep && ticks == 200 &&
            ((supportOffset == 0.0 && tick <= 8) || (supportOffset == -0.15 && tick >= 75 && tick <= 85))) {
            const Vec3 triDelta = (triangle.velocity - triangleBefore) * triangle.mass;
            const Vec3 cubeDelta = (cube.velocity - cubeBefore) * cube.mass;
            std::printf("STACK LEDGER %.2f tick %d dE %+.8f tri dp (%+.6f,%+.6f) sum (%+.6f,%+.6f) "
                        "cube dp (%+.6f,%+.6f) sum (%+.6f,%+.6f) pair (%+.6f,%+.6f)\n",
                        supportOffset, tick, localChange, triDelta.x, triDelta.y, triangleImpulse.x, triangleImpulse.y,
                        cubeDelta.x, cubeDelta.y, cubeImpulse.x, cubeImpulse.y, pairImpulse.x, pairImpulse.y);
            for (std::size_t sample = 0; sample < room.forceCount(); ++sample) {
                const auto& force = room.forces()[sample];
                if (force.couple || (force.owner != PlaygroundRoom::kTriangle && force.owner != PlaygroundRoom::kCube))
                    continue;
                std::printf("  owner %zu kind %d impulse (%+.6f,%+.6f) at (%+.6f,%+.6f)\n",
                            force.owner, int(force.kind), force.force.x / 20.0, force.force.y / 20.0,
                            force.at.x, force.at.y);
            }
        }
    }
    char detail[400];
    std::snprintf(detail, sizeof detail,
                  "%d ticks, angle %.2f, offset %+.2f, support %s: ball impulse %.5f N s, "
                  "overlap %.6g m at %d, stretch %.6g m, room/island gains %.6g J at %d / %.6g J at %d",
                  ticks, angle, supportOffset, supportAsleep ? "asleep" : "awake", meeting, overlap, overlapTick,
                  stretch, gain, worstTick, islandGain, worstIslandTick);
    check(meeting > 1e-5 && overlap < 1.1e-6 && stretch < 1.1e-6 && gain < 1e-4 && islandGain < 1e-4,
          "a rope-ball contact keeps a supporting stack solid and does not create energy", detail);
    std::snprintf(detail, sizeof detail,
                  "%d ticks, angle %.2f, offset %+.2f, support %s: peak body momentum error %.6g N s at %d; "
                  "net pair impulse %.6g N s",
                  ticks, angle, supportOffset, supportAsleep ? "asleep" : "awake", momentumError, momentumTick,
                  pairError);
    check(momentumError < 1e-3 && pairError < 1e-3,
          "the stack's recorded impulses account for momentum on both bodies", detail);
}

void supportedStackChecks() {
    if (std::getenv("GTC_TRACE_STACK_LEDGER")) {
        ropeAgainstSupportedStack(-1.00, 0.0, false, 200);
        ropeAgainstSupportedStack(-1.00, -0.15, false, 200);
        return;
    }
    for (double angle : {-1.30, -1.00, -0.65})
        for (double offset : {-0.15, 0.0, 0.15})
            for (bool asleep : {false, true})
                ropeAgainstSupportedStack(angle, offset, asleep, 200);
    ropeAgainstSupportedStack(-1.00, 0.0, false, 3000);
}
