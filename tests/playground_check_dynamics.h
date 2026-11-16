// A shape and a rope ball should share the same physical time interval. Compare
// an airborne shape sliding past the ball to an otherwise identical free fall;
// with no tangential grip and a vertical face, the ball cannot add a second
// downward acceleration. Check the shape's recorded impulse against its momentum
// change as well as the motion itself.
void fallingShapeAgainstRope() {
    struct Result {
        double vy, centreY, gap, energyChange;
        Vec3 ballImpulse, deltaMomentum, recorded;
    };
    const auto run = [](bool touching, std::size_t bodyIndex, double initialSpeed, double clearance) {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        auto& other = room.ball(PlaygroundRoom::kThrowBall);
        other.position = other.previous = other.shown = {-3.0, other.radius, 0};
        other.velocity = {};
        for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
            if (i == bodyIndex) continue;
            auto& parked = room.bodies_[i];
            parked.centre = parked.previousCentre = {-20.0 - 5.0 * double(i), 3.0, 0};
            if (parked.pivoted) parked.pivotPoint = parked.centre;
            parked.velocity = {};
            parked.spin = 0;
            parked.frozen = true;
            room.placeCorners(parked);
            parked.previous = parked.shown = parked.position;
        }
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

        auto& body = room.bodies_[bodyIndex];
        double left = 0.0;
        for (std::size_t i = 0; i < body.count; ++i) left = std::min(left, body.local[i].x);
        body.centre = body.previousCentre = {target.x + ball.radius + clearance - left, target.y, 0};
        body.angle = body.previousAngle = 0;
        body.velocity = {0, initialSpeed, 0};
        body.spin = 0;
        body.staticFriction = body.kineticFriction = 0;
        body.frozen = false;
        room.placeCorners(body);
        body.previous = body.shown = body.position;
        const Vec3 initialVelocity = body.velocity;
        const double initialEnergy = room.totalEnergy();
        room.advance(1.0 / 20.0);
        Vec3 sum{}, fromBall{};
        for (std::size_t i = 0; i < room.forceCount(); ++i) {
            const auto& force = room.forces()[i];
            if (force.owner != bodyIndex || force.couple) continue;
            sum += force.force * (1.0 / 20.0);
            if (force.kind == PlaygroundRoom::ForceKind::Ball) fromBall += force.force * (1.0 / 20.0);
        }
        Vec3 way{}, at{};
        const double gap = room.bodyBallGap(ball, bodyIndex, way, at);
        return Result{body.velocity.y, body.centre.y, gap, room.totalEnergy() - initialEnergy, fromBall,
                      (body.velocity - initialVelocity) * body.mass, sum};
    };
    int contacts = 0;
    for (std::size_t body : {PlaygroundRoom::kCube, PlaygroundRoom::kTriangle}) {
        for (double speed : {-4.0, -1.0, 1.0}) {
            for (double gap : {0.0, 0.005}) {
                const Result reference = run(false, body, speed, gap), contact = run(true, body, speed, gap);
                char detail[320];
                const double mass = body == PlaygroundRoom::kCube ? 4.0 : 3.0;
                const bool met = length(contact.ballImpulse) > 1e-6;
                contacts += int(met);
                const double unexplainedY = contact.vy - reference.vy - contact.ballImpulse.y / mass;
                std::snprintf(detail, sizeof detail,
                              "body %zu initial vy %+.1f, clearance %.3f: free/contact vy %.5f/%.5f, "
                              "height difference %+.6f m, gap %.6f m, impulse (%+.5f,%+.5f) N s, "
                              "unexplained vy %+.5f, dE %+.5f J, momentum error %.5f N s",
                              body, speed, gap, reference.vy, contact.vy, contact.centreY - reference.centreY,
                              contact.gap, contact.ballImpulse.x, contact.ballImpulse.y, unexplainedY,
                              contact.energyChange, length(contact.deltaMomentum - contact.recorded));
                // Fast air drag can move the triangle's face away from the ball;
                // a separating pair is not required to exchange an impulse. The
                // legacy drag arrow is the initial force, not its integrated mean,
                // so use the free-fall reference's own ledger error as allowance.
                const double airDisplayError = length(reference.deltaMomentum - reference.recorded);
                check(std::abs(unexplainedY) < 0.03 && std::abs(contact.centreY - reference.centreY) < 0.005 &&
                          length(contact.deltaMomentum - contact.recorded) < airDisplayError + 0.03 &&
                          contact.gap >= -1.1e-6 && contact.energyChange < 0.005,
                      "a rope-ball neighbour does not give an airborne shape an extra tick of gravity", detail);
            }
        }
    }
    char detail[120];
    std::snprintf(detail, sizeof detail, "%d of 12 trials exchanged a ball/body impulse", contacts);
    check(contacts >= 8, "the falling-shape matrix actually exercises contact", detail);
}
