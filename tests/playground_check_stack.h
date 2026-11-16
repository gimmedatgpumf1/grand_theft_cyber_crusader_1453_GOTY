// A ball and a stack must not be solved as if the supporting body disappeared
// when the ball's sustained-contact step takes over.
void ropeAgainstSupportedStack() {
    PlaygroundRoom room;
    room.reset();
    settle(room, 200);
    auto& other = room.ball(PlaygroundRoom::kThrowBall);
    other.position = other.previous = other.shown = {-3, other.radius, 0};
    other.velocity = {};
    room.point_ = room.previous_ = {-4, 4, 0};
    room.velocity_ = {};
    room.pointFrozen_ = true;

    auto& plank = room.bodies_[PlaygroundRoom::kSeesaw];
    plank.centre = plank.previousCentre = plank.pivotPoint = {-20, 3, 0};
    plank.velocity = {};
    plank.spin = 0;
    plank.frozen = true;
    room.placeCorners(plank);
    plank.previous = plank.shown = plank.position;

    auto& ball = room.ball(PlaygroundRoom::kRopeBall);
    const double angle = -1.0;
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
    cube.centre = cube.previousCentre = {triangle.centre.x, 0.35, 0};
    cube.angle = cube.previousAngle = 0;
    cube.velocity = {};
    cube.spin = 0;
    cube.frozen = false;
    room.placeCorners(cube);
    cube.previous = cube.shown = cube.position;

    double overlap = 0, gain = 0, stretch = 0, meeting = 0;
    int worstTick = 0;
    for (int tick = 1; tick <= 200; ++tick) {
        const double energy = room.totalEnergy();
        room.advance(1.0 / 20.0);
        pair_shape::PairManifold patch;
        if (pair_shape::collide(triangle.position.data(), triangle.count, cube.position.data(), cube.count, 0, patch))
            overlap = std::max(overlap, patch.overlap);
        const double change = room.totalEnergy() - energy;
        if (change > gain) { gain = change; worstTick = tick; }
        stretch = std::max(stretch, room.ropeBallDistance() - room.ropeLength());
        for (std::size_t i = 0; i < room.forceCount(); ++i) {
            const auto& force = room.forces()[i];
            if (force.owner == PlaygroundRoom::kTriangle && force.kind == PlaygroundRoom::ForceKind::Ball)
                meeting = std::max(meeting, length(force.force) / 20.0);
        }
    }
    char detail[240];
    std::snprintf(detail, sizeof detail,
                  "200 ticks: ball impulse %.5f N s, stack overlap %.6g m, rope stretch %.6g m, "
                  "largest single-tick energy gain %.6g J at tick %d",
                  meeting, overlap, stretch, gain, worstTick);
    check(meeting > 1e-5 && overlap < 1.1e-6 && stretch < 1.1e-6 && gain < 1e-4,
          "a rope-ball contact keeps a supporting stack solid and does not create energy", detail);
}
