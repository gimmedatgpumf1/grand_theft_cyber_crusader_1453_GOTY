// Ball-impact fixtures share the arrival test reporting and room setup.
// Recreate the user's picture: the rope is almost horizontal to the left, and
// the triangle stands on its long bottom edge, with its vertical side just left
// of the anchor. The ball is placed by the same mouse ray used by the client.
void horizontalRopeImpact(int fps, bool awake, double sideOffset) {
    using gtc::native::PlaygroundRoom;
    using gtc::native::Vec3;
    using gtc::native::dot;
    using gtc::native::length;

    PlaygroundRoom room;
    room.reset();
    settle(room, 200);
    auto& other = room.ball(PlaygroundRoom::kThrowBall);
    other.position = other.previous = other.shown = Vec3{-3.0, other.radius, 0.0};
    other.velocity = {};
    other.spin = 0.0;
    other.frozen = false;

    const Vec3 anchor = room.ropeAnchor();
    auto& triangle = room.bodies_[PlaygroundRoom::kTriangle];
    triangle.centre = triangle.previousCentre = Vec3{anchor.x - sideOffset + 0.7, 0.7, 0.0};
    triangle.angle = triangle.previousAngle = 0.0;
    triangle.velocity = {};
    triangle.spin = 0.0;
    triangle.frozen = !awake;
    room.placeCorners(triangle);
    triangle.previous = triangle.shown = triangle.position;
    const auto startingCorners = triangle.position;
    const Vec3 startingCentre = triangle.centre;

    auto& ball = room.ball(PlaygroundRoom::kRopeBall);
    const double rope = room.ropeLength();
    const auto placeAt = [&](double angle) {
        return anchor + Vec3{std::sin(angle), -std::cos(angle), 0.0} * rope;
    };
    const Vec3 first = placeAt(-1.44);
    const Vec3 release = placeAt(-1.54);
    ball.position = ball.previous = ball.shown = first;
    ball.velocity = {};
    ball.spin = 0.0;
    ball.frozen = false;
    ball.touching = false;
    room.rope_.taut = true;
    room.setDragRay(first + Vec3{0, 0, 5}, Vec3{0, 0, -1});
    room.beginDrag();
    const bool grabbed = room.dragTarget_ == PlaygroundRoom::DragTarget::Ball &&
                         room.dragBall_ == PlaygroundRoom::kRopeBall;
    room.setDragRay(release + Vec3{0, 0, 5}, Vec3{0, 0, -1});
    room.advance(1.0 / fps);
    room.endDrag();
    const double releaseError = length(ball.position - release);
    const double releaseSpeed = length(ball.velocity);
    const double releaseSpin = std::abs(ball.spin);

    bool hit = false;
    int tick = 0, impactTick = -1;
    double incoming = 0.0, outgoing = 0.0, impactBallX = 0.0, lowestX = 0.0, separation = 0.0;
    double contactIn = 0.0, contactOut = 0.0;
    double triangleMove = 0.0, maxGain = 0.0, overlap = 0.0, stretch = 0.0;
    int gainTick = 0;
    const bool trace = std::getenv("GTC_TRACE_HORIZONTAL") != nullptr && fps == 60 && !awake;
    for (int frame = 0; frame < fps * 4; ++frame) {
        Vec3 way{}, at{};
        const double beforeGap = room.bodyBallGap(ball, PlaygroundRoom::kTriangle, way, at);
        const Vec3 normal = way * -1.0;
        const double beforeRelative = dot(ball.velocity - room.pointSpeed(triangle, at), normal);
        const double beforeEnergy = room.totalEnergy();
        const Vec3 triangleSpeedBefore = triangle.velocity;
        const bool stepped = std::min(room.accumulator_ + 1.0 / fps, 0.25) >= 1.0 / 20.0;
        room.advance(1.0 / fps);
        if (!stepped) continue;
        ++tick;
        if (std::getenv("GTC_TRACE_LEDGER") && fps == 60 && !awake && sideOffset == 0.12 && tick >= 30 && tick <= 55) {
            Vec3 recorded{}, weight{}, floor{}, blow{};
            for (std::size_t i = 0; i < room.forceCount(); ++i) {
                const auto& f = room.forces()[i];
                if (f.owner != PlaygroundRoom::kTriangle || f.couple) continue;
                const Vec3 impulse = f.force * (1.0 / 20.0);
                recorded += impulse;
                if (f.kind == PlaygroundRoom::ForceKind::Gravity) weight += impulse;
                if (f.kind == PlaygroundRoom::ForceKind::Contact) floor += impulse;
                if (f.kind == PlaygroundRoom::ForceKind::Ball) blow += impulse;
            }
            const Vec3 delta = (triangle.velocity - triangleSpeedBefore) * triangle.mass;
            std::printf("   ledger tick %d dp (%+.6f,%+.6f) impulse (%+.6f,%+.6f) weight %+.6f floor %+.6f blow (%+.6f,%+.6f)\n",
                        tick, delta.x, delta.y, recorded.x, recorded.y, weight.y, floor.y, blow.x, blow.y);
        }
        const double gap = room.bodyBallGap(ball, PlaygroundRoom::kTriangle, way, at);
        const double relative = dot(ball.velocity - room.pointSpeed(triangle, at), way * -1.0);
        const double gain = room.totalEnergy() - beforeEnergy;
        if (gain > maxGain) { maxGain = gain; gainTick = tick; }
        overlap = std::max(overlap, -gap);
        stretch = std::max(stretch, room.ropeBallDistance() - rope);
        if (std::getenv("GTC_TRACE_BOUNDS") && fps == 60 &&
            (room.ropeBallDistance() - rope > 1e-6 || gap < -1.1e-6 || gain > 1e-4))
            std::printf("   bounds offset %.2f %s tick %d stretch %.7f gap %+.7f dE %+.7f "
                        "pos (%+.5f,%+.5f) v (%+.5f,%+.5f) taut %d hit %.5f -> %.5f\n",
                        sideOffset, awake ? "awake" : "asleep", tick, room.ropeBallDistance() - rope, gap, gain,
                        ball.position.x, ball.position.y, ball.velocity.x, ball.velocity.y, int(room.ropeTaut()),
                        ball.meetingSpeedIn[PlaygroundRoom::kTriangle], ball.meetingSpeedOut[PlaygroundRoom::kTriangle]);
        bool impulse = ball.meetingArrival[PlaygroundRoom::kTriangle] > 1e-7;
        for (std::size_t i = 0; i < room.forceCount(); ++i) {
            const auto& force = room.forces()[i];
            if (force.owner == PlaygroundRoom::kTriangle && force.kind == PlaygroundRoom::ForceKind::Ball &&
                length(force.force) > 1e-4)
                impulse = true;
        }
        if (!hit && impulse && beforeRelative < -0.1 && beforeGap < 0.3) {
            hit = true;
            impactTick = tick;
            incoming = beforeRelative;
            outgoing = dot(ball.velocity - room.pointSpeed(triangle, at), normal);
            contactIn = ball.meetingSpeedIn[PlaygroundRoom::kTriangle];
            contactOut = ball.meetingSpeedOut[PlaygroundRoom::kTriangle];
            impactBallX = ball.velocity.x;
            if (trace) {
                std::printf("   impact: began (%+.4f,%+.4f), alone (%+.4f,%+.4f), "
                            "blows (%+.4f,%+.4f) N s, bend %.4f at (%+.4f,%+.4f), "
                            "meeting %.4f / %.4f N s, spin %+.4f rad/s, impacted %d, "
                            "answered %d, bent %d, before gap %.7f, offset %.2f\n",
                            ball.began.x, ball.began.y, ball.alone.x, ball.alone.y,
                            ball.blows.x, ball.blows.y, ball.bendAt, ball.bendPoint.x, ball.bendPoint.y,
                            ball.meetingGiven[PlaygroundRoom::kTriangle],
                            ball.meetingArrival[PlaygroundRoom::kTriangle], ball.spin,
                            int(ball.impacted), int(ball.answered), int(ball.bent), beforeGap, sideOffset);
            }
        }
        if (hit) {
            lowestX = std::min(lowestX, ball.velocity.x);
            separation = std::max(separation, gap);
            triangleMove = std::max(triangleMove, length(triangle.centre - startingCentre));
            for (std::size_t i = 0; i < triangle.count; ++i)
                triangleMove = std::max(triangleMove, length(triangle.position[i] - startingCorners[i]));
        }
        if (trace && tick <= 50)
            std::printf("   tick %02d gap %+.6f rel %+.4f -> %+.4f ball (%+.3f,%+.3f) "
                        "triangle (%+.3f,%+.3f) spin %+.3f dE %+.7f impulse %d\n",
                        tick, gap, beforeRelative, relative, ball.velocity.x, ball.velocity.y,
                        triangle.velocity.x, triangle.velocity.y, triangle.spin, gain, int(impulse));
    }
    char detail[400];
    std::snprintf(detail, sizeof detail,
                  "%d Hz %s offset %.2f: hit %d at tick %d, normal %.3f -> %.3f m/s, "
                  "impact vx %.3f m/s, min vx %.3f, separation %.3f m, triangle moved %.4f m, "
                  "gain %.6f J at tick %d, overlap %.3g m, stretch %.3g m",
                  fps, awake ? "awake" : "asleep", sideOffset, int(hit), impactTick,
                  incoming, outgoing, impactBallX, lowestX, separation, triangleMove, maxGain, gainTick,
                  overlap, stretch);
    check(grabbed && releaseError < 1e-6 && releaseSpeed < 1e-9 && releaseSpin < 1e-9,
          "a mouse release places the rope ball without throwing it", detail);
    check(hit && incoming < -0.5 && outgoing > 0.2 && impactBallX < -0.2 && separation > 0.1,
          "the horizontal rope swing rebounds visibly from the triangle", detail);
    char impactDetail[180];
    std::snprintf(impactDetail, sizeof impactDetail, "%d Hz %s offset %.2f: contact normal %.4f -> %.4f m/s",
                  fps, awake ? "awake" : "asleep", sideOffset, contactIn, contactOut);
    check(contactIn < -0.5 && contactOut > 0.0 &&
              std::abs(contactOut + ball_shape::kBallRestitution * contactIn) < 1e-3,
          "the contact itself has the material's relative restitution", impactDetail);
    check(hit && triangleMove > 0.001,
          "the triangle reacts at its centre or corners", detail);
    check(maxGain < 1e-4 && overlap < 1.1e-6 && stretch < 1.1e-6,
          "the swing does not gain energy, penetrate the triangle or stretch the rope", detail);
}

// A rolling ball reaches the cube's side while both are supported by the floor.
// Measure motion, not just the restitution bookkeeping: a recorded bounce is not
// useful if the subsequent solve leaves the ball moving into the cube.
void floorBallHitsCube(double speed, bool awake, double initialGap) {
    using gtc::native::PlaygroundRoom;
    using gtc::native::Vec3;
    using gtc::native::dot;
    using gtc::native::length;

    PlaygroundRoom room;
    room.reset();
    settle(room, 200);
    auto& cube = room.bodies_[PlaygroundRoom::kCube];
    cube.centre = cube.previousCentre = Vec3{1.2, 0.35, 0};
    cube.angle = cube.previousAngle = 0.0;
    cube.velocity = {};
    cube.spin = 0.0;
    cube.frozen = !awake;
    room.placeCorners(cube);
    cube.previous = cube.shown = cube.position;
    const Vec3 start = cube.centre;
    const auto corners = cube.position;
    auto& ball = room.ball(PlaygroundRoom::kThrowBall);
    ball.position = ball.previous = ball.shown = Vec3{1.2 - 0.35 - ball.radius - initialGap, ball.radius, 0};
    ball.velocity = Vec3{speed, 0, 0};
    ball.spin = -speed / ball.radius;
    ball.frozen = false;
    ball.touching = ball.standing = true;
    ball.supportNormal = Vec3{0, 1, 0};

    bool hit = false;
    double incoming = 0.0, outgoing = 0.0, impactX = 0.0, maxGap = 0.0;
    double movement = 0.0, penetration = 0.0, maxGain = 0.0;
    int impactTick = -1;
    for (int tick = 1; tick <= 60; ++tick) {
        Vec3 way{}, at{};
        const double beforeGap = room.bodyBallGap(ball, PlaygroundRoom::kCube, way, at);
        const Vec3 normal = way * -1.0;
        const double beforeRelative = dot(ball.velocity - room.pointSpeed(cube, at), normal);
        const double energy = room.totalEnergy();
        room.advance(1.0 / 20.0);
        const double gap = room.bodyBallGap(ball, PlaygroundRoom::kCube, way, at);
        if (std::getenv("GTC_TRACE_CUBE") && speed == 1.0 && initialGap == 0.03 && tick <= 8) {
            std::printf("   cube %s tick %d gap %+.7f ball v (%+.6f,%+.6f) spin %+.5f "
                        "cube x %.7f v (%+.6f,%+.6f) spin %+.5f contact %.6f/%.6f\n",
                        awake ? "awake" : "asleep", tick, gap, ball.velocity.x, ball.velocity.y, ball.spin,
                        cube.centre.x, cube.velocity.x, cube.velocity.y, cube.spin,
                        ball.meetingGiven[PlaygroundRoom::kCube], ball.meetingArrival[PlaygroundRoom::kCube]);
            for (std::size_t i = 0; i < room.forceCount(); ++i) {
                const auto& f = room.forces()[i];
                if (f.owner == PlaygroundRoom::kCube)
                    std::printf("      force kind %d at (%+.5f,%+.5f) value (%+.5f,%+.5f)\n",
                                int(f.kind), f.at.x, f.at.y, f.force.x, f.force.y);
            }
        }
        maxGain = std::max(maxGain, room.totalEnergy() - energy);
        penetration = std::max(penetration, -gap);
        bool impulse = ball.meetingArrival[PlaygroundRoom::kCube] > 1e-7;
        for (std::size_t i = 0; i < room.forceCount(); ++i) {
            const auto& force = room.forces()[i];
            if (force.owner == PlaygroundRoom::kCube && force.kind == PlaygroundRoom::ForceKind::Ball &&
                length(force.force) > 1e-4) impulse = true;
        }
        if (!hit && impulse && beforeRelative < -0.1 && beforeGap < 0.3) {
            hit = true;
            impactTick = tick;
            incoming = beforeRelative;
            outgoing = dot(ball.velocity - room.pointSpeed(cube, at), normal);
            impactX = ball.velocity.x;
        }
        if (hit) {
            maxGap = std::max(maxGap, gap);
            movement = std::max(movement, length(cube.centre - start));
            for (std::size_t i = 0; i < cube.count; ++i)
                movement = std::max(movement, length(cube.position[i] - corners[i]));
        }
    }
    char detail[320];
    std::snprintf(detail, sizeof detail,
                  "%.1f m/s %s gap %.2f m: hit %d at tick %d, normal %.3f -> %.3f m/s, "
                  "ball vx %.3f m/s, separation %.4f m, cube moved %.4f m, overlap %.3g m, gain %.6f J",
                  speed, awake ? "awake" : "asleep", initialGap, int(hit), impactTick,
                  incoming, outgoing, impactX, maxGap, movement, penetration, maxGain);
    check(hit && outgoing > 0.1 && impactX < -0.1 && maxGap > 0.03,
          "a rolling ball rebounds and separates from the cube", detail);
    // An impact transfers momentum to the cube as well as reversing the ball.
    check(hit && movement > 0.001 && penetration < 1.1e-6,
          "the floor-supported cube does not let the ball through", detail);
}

