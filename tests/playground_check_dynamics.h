// Checks that a shape and a rope ball share a consistent time interval when
// contact begins. The matching no-contact run supplies the air-drag reference.
void fallingShapeAgainstRope() {
    struct Result { double vy, centreY, gap; Vec3 deltaMomentum, recorded; };
    const auto run = [](bool touching) {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        auto& other = room.ball(PlaygroundRoom::kThrowBall);
        other.position = other.previous = other.shown = {-3.0, other.radius, 0};
        other.velocity = {};
        for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i)
            if (i != PlaygroundRoom::kTriangle) room.bodies_[i].count = 0;
        room.point_ = room.previous_ = {-4, 4, 0};
        room.velocity_ = {};
        room.pointFrozen_ = true;

        auto& ball = room.ball(PlaygroundRoom::kRopeBall);
        const double angle = -0.9;
        const Vec3 target = room.ropeAnchor() +
                            Vec3{std::sin(angle), -std::cos(angle), 0} * room.ropeLength();
        const double ballAngle = touching ? angle : -1.4;
        ball.position = ball.previous = ball.shown = room.ropeAnchor() +
                        Vec3{std::sin(ballAngle), -std::cos(ballAngle), 0} * room.ropeLength();
        ball.velocity = {};
        ball.spin = 0;
        ball.frozen = false;
        ball.touching = false;
        room.rope_.taut = true;

        auto& body = room.bodies_[PlaygroundRoom::kTriangle];
        body.centre = body.previousCentre = {target.x + ball.radius + 0.7, target.y, 0};
        body.angle = body.previousAngle = 0;
        body.velocity = {0, -1, 0};
        body.spin = 0;
        body.staticFriction = body.kineticFriction = 0;
        body.frozen = false;
        room.placeCorners(body);
        body.previous = body.shown = body.position;
        const Vec3 initialVelocity = body.velocity;
        room.advance(1.0 / 20.0);
        Vec3 sum{};
        for (std::size_t i = 0; i < room.forceCount(); ++i) {
            const auto& force = room.forces()[i];
            if (force.owner == PlaygroundRoom::kTriangle && !force.couple) sum += force.force * (1.0 / 20.0);
        }
        Vec3 way{}, at{};
        const double gap = room.bodyBallGap(ball, PlaygroundRoom::kTriangle, way, at);
        return Result{body.velocity.y, body.centre.y, gap, (body.velocity - initialVelocity) * body.mass, sum};
    };
    const Result reference = run(false), contact = run(true);
    char detail[250];
    std::snprintf(detail, sizeof detail,
                  "airborne shape: free vy %.6f, contact vy %.6f m/s, gap %.6f m, "
                  "momentum y change %.6f and recorded impulse %.6f N s",
                  reference.vy, contact.vy, contact.gap, contact.deltaMomentum.y, contact.recorded.y);
    check(contact.gap < 0.01 && std::abs(contact.vy - reference.vy) < 0.02 &&
              std::abs(contact.deltaMomentum.y - contact.recorded.y) < 0.02,
          "a sustained contact does not apply a second tick of gravity to a falling shape", detail);
}
