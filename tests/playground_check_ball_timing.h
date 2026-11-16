// A side impact must not apply the remainder of a tick's gravity twice. Compare
// a ball that hits a stationary face with the same ball in unimpeded flight, and
// account for every non-gravity impulse actually recorded on the ball.
void ordinaryBallImpactGravity() {
    struct Result {
        Vec3 velocity{}, recorded{}, nonGravity{};
        double arrival = 0.0, mass = 0.0, energyChange = 0.0;
    };
    const auto run = [](double gap, bool hits) {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        room.point_ = room.previous_ = {-4, 0, 0};
        room.velocity_ = {};
        room.resting_ = true;
        room.restingBody_ = -1;
        room.restingOnBall_ = false;
        room.pointFrozen_ = true;
        room.rope_.attached = false;
        auto& other = room.ball(PlaygroundRoom::kRopeBall);
        other.position = other.previous = other.shown = {-8, other.radius, 0};
        other.velocity = {};
        other.spin = 0;
        other.frozen = true;
        for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
            if (i == PlaygroundRoom::kCube) continue;
            auto& body = room.bodies_[i];
            body.centre = body.previousCentre = {-20.0 - 5.0 * double(i), 3.0, 0};
            if (body.pivoted) body.pivotPoint = body.centre;
            body.velocity = {};
            body.spin = 0;
            body.frozen = true;
            room.placeCorners(body);
            body.previous = body.shown = body.position;
        }
        auto& wall = room.bodies_[PlaygroundRoom::kCube];
        wall.centre = wall.previousCentre = {0.35, 2.0, 0};
        wall.angle = wall.previousAngle = 0;
        wall.velocity = {};
        wall.spin = 0;
        wall.frozen = false;
        room.placeCorners(wall);
        wall.previous = wall.shown = wall.position;
        // Keep the wall stationary without applying a moving hand velocity.
        room.dragTarget_ = PlaygroundRoom::DragTarget::Body;
        room.dragBody_ = PlaygroundRoom::kCube;
        room.setDragRay({0, 0, 1}, {0, 0, 0});

        auto& ball = room.ball(PlaygroundRoom::kThrowBall);
        ball.position = ball.previous = ball.shown = {-ball.radius - gap - (hits ? 0.0 : 1.0), 2.0, 0};
        ball.velocity = {1.0, 0, 0};
        ball.spin = 0;
        ball.frozen = false;
        ball.touching = false;
        const double before = room.totalEnergy();
        room.advance(1.0 / 20.0);
        Vec3 recorded{}, nonGravity{};
        for (std::size_t i = 0; i < room.forceCount(); ++i) {
            const auto& force = room.forces()[i];
            if (force.owner != PlaygroundRoom::ballOwner(PlaygroundRoom::kThrowBall) || force.couple) continue;
            const Vec3 impulse = force.force * (1.0 / 20.0);
            recorded += impulse;
            if (force.kind != PlaygroundRoom::ForceKind::Gravity) nonGravity += impulse;
        }
        return Result{ball.velocity, recorded, nonGravity, ball.meetingArrival[PlaygroundRoom::kCube],
                      ball.mass, room.totalEnergy() - before};
    };
    for (double gap : {0.005, 0.015, 0.035}) {
        const Result free = run(gap, false), hit = run(gap, true);
        const double missing = (hit.velocity.y - free.velocity.y) * hit.mass -
                               (hit.nonGravity.y - free.nonGravity.y);
        const double ledgerError = hit.velocity.y * hit.mass - hit.recorded.y;
        char detail[240];
        std::snprintf(detail, sizeof detail,
                      "gap %.3f m: hit %d, free/hit vy %+.5f/%+.5f m/s, unexplained impulse %+.6f N s, "
                      "ledger error %+.6f N s, dE %+.6f J",
                      gap, int(hit.arrival > 1e-6), free.velocity.y, hit.velocity.y, missing, ledgerError,
                      hit.energyChange);
        check(hit.arrival > 1e-6 && hit.velocity.x < 0 && std::abs(missing) < 1e-4 &&
                  std::abs(ledgerError) < 1e-4 && hit.energyChange < 1e-4,
              "a side impact does not apply extra gravity to a falling ball", detail);
    }
}
