// The toilet, the needs, the puddles and what the weather does to them.

#include "harness.h"

namespace {

void runLowTunnelPreservesManually() {
    test("low tunnel preserves manually selected prone crawling", [] {
        gtc::World world;
        const auto id = world.addCharacter("Tunnel Subject", false);
        check(driveTo(world, id, {-5.0F, 1.4F}), "approach the low tunnel");
        bool unsolicitedStance = false;
        for (int i = 0; i < 45; ++i) {
            world.setInput(id, {0, -1}, false, false, 3.14159265F, 0);
            world.update(0.05F);
            unsolicitedStance = unsolicitedStance || world.character(id).prone || world.character(id).crouched;
        }
        check(!unsolicitedStance, "walking into low clearance never initiates crouch or prone");
        check(world.character(id).position.y > 1.0F, "standing profile is stopped by the tunnel");

        for (int i = 0; i < 90 && world.character(id).position.y > 0.25F; ++i) {
            world.setInput(id, {0, -1}, false, false, 3.14159265F, 0, false, true, true);
            world.update(0.05F);
        }
        check(world.character(id).prone && world.character(id).position.y < 0.30F,
              "manual prone enters the low tunnel");
        bool correctSpeed = true;
        world.setInput(id, {0, -1}, false, false, 3.14159265F, 0);
        world.update(0.05F);
        check(world.character(id).prone, "low clearance preserves prone after stance keys are released");
        for (int i = 0; i < 180 && world.character(id).position.y > -1.35F; ++i) {
            world.setInput(id, {0, -1}, false, false, 3.14159265F, 0);
            world.update(0.05F);
            if (world.character(id).prone && std::abs(world.character(id).speed * 3.6F - 2.0F) > 0.001F) {
                correctSpeed = false;
            }
        }
        check(correctSpeed, "continued tunnel crawling uses two kilometres per hour");
        check(world.character(id).position.y < -1.2F, "prone profile physically passes through the tunnel");
        check(!world.character(id).prone && !world.character(id).crouched,
              "citizen stands automatically after leaving the tunnel");
    });
}

void runShittingDrainsBowelWithout() {
    test("shitting drains bowel without magically emptying bladder", [] {
        gtc::World world;
        const auto id = world.addCharacter("Bowel Subject", false);
        moveToToilet(world, id);
        world.adjustNeed(id, "bowel", 100);
        const float bladder = world.character(id).needs.bladder;
        world.requestToilet(id, "shit");
        check(world.character(id).onToilet, "shitting is seated");
        for (int i = 0; i < 700 && world.character(id).toiletAction != gtc::ToiletAction::None; ++i) {
            world.update(0.05F);
        }
        const auto relieved = world.character(id);
        check(relieved.needs.bowel == 0.0F, "shitting empties bowel");
        check(relieved.needs.bladder == bladder, "shitting leaves bladder unchanged");
        check(relieved.toiletAction == gtc::ToiletAction::None, "completed shit releases the throne");
    });
}

void runMaleToiletPissIs() {
    test("male toilet piss is standing and makes no puddle", [] {
        gtc::World world;
        const auto id = world.addCharacter("Standing Subject", false);
        moveToToilet(world, id);
        world.adjustNeed(id, "bladder", 100);
        const float bowel = world.character(id).needs.bowel;
        world.requestToilet(id, "piss");
        check(!world.character(id).onToilet, "male toilet piss is standing");
        check(world.character(id).pissing, "standing toilet piss has a visible stream state");
        world.update(0.05F);
        const auto launch = world.character(id);
        const auto drops = world.projectiles();
        bool replicatedLaunch = !drops.empty() && world.snapshot().find("\"pissPitch\":") != std::string::npos;
        if (!drops.empty()) {
            const auto& drop = drops.front();
            replicatedLaunch = replicatedLaunch && std::abs(launch.pissPitch -
                std::atan2(drop.verticalVelocity + gtc::kProjectileGravity * drop.age,
                           gtc::length(drop.velocity))) < 0.00001F &&
                std::abs(std::sin(launch.pissYaw - std::atan2(drop.velocity.x, drop.velocity.y))) < 0.00001F;
        }
        check(replicatedLaunch, "posed organ receives the exact physical stream yaw and pitch");
        check(launch.pissPitch < 0.0F,
              "toilet stream descends from the meatus instead of lobbing up over the body");
        for (int i = 1; i < 700 && world.character(id).toiletAction != gtc::ToiletAction::None; ++i) {
            world.update(0.05F);
        }
        const auto relieved = world.character(id);
        check(relieved.needs.bladder == 0.0F, "toilet piss empties bladder");
        check(relieved.needs.bowel == bowel, "pissing leaves bowel unchanged");
        check(world.snapshot().find("\"puddles\":[{\"id\":") == std::string::npos,
              "pissing into the bowl must not create a floor puddle");
    });
}

void runCreatorValidatesAppearanceAnd() {
    test("creator validates appearance and female piss is seated", [] {
        gtc::World world;
        const auto id = world.addCharacter("Creator Subject", false);
        gtc::Appearance appearance;
        appearance.male = false;
        appearance.skin = 99;
        appearance.hair = 99;
        appearance.hairColor = 99;
        appearance.morphs.fill(500);
        appearance.morphs[0] = -20;
        appearance.morphs[3] = -500;
        appearance.shirt = 99;
        appearance.pants = -2;
        appearance.beard = 99;
        appearance.coat = true;
        appearance.sunglasses = true;
        world.customizeCharacter(id, appearance);
        const auto customized = world.character(id);
        check(!customized.appearance.male && customized.appearance.skin == 5 &&
              customized.appearance.hair == 15 && customized.appearance.shirt == 6 &&
              customized.appearance.pants == 0 && customized.appearance.beard == 2 &&
              customized.appearance.hairColor == 8,
              "creator fields are validated and clamped");
        check(customized.appearance.morphs[0] == 0 && customized.appearance.morphs[1] == 100 &&
              customized.appearance.morphs[3] == -100 && customized.appearance.morphs[50] == 100,
              "all 51 morph values are server-clamped");
        check(world.snapshot().find("\"morphs\":[0,100,100,-100") != std::string::npos,
              "the morph vector is replicated in snapshots");
        check(world.snapshot().find("\"coat\":true") != std::string::npos,
              "appearance is replicated in snapshots");
        moveToToilet(world, id);
        world.requestToilet(id, "piss");
        check(world.character(id).onToilet && !world.character(id).pissing,
              "female toilet piss uses the seated pose without a standing stream");
        gtc::Appearance male = customized.appearance;
        male.male = true;
        world.customizeCharacter(id, male);
        check(world.character(id).toiletAction == gtc::ToiletAction::None && !world.character(id).onToilet,
              "changing sex interrupts the old seated piss pose");
        world.requestToilet(id, "piss");
        check(!world.character(id).onToilet && world.character(id).pissing,
              "male piss starts standing after customization");
        world.customizeCharacter(id, customized.appearance);
        check(world.character(id).toiletAction == gtc::ToiletAction::None,
              "changing sex also interrupts standing piss");
    });
}

void runCheatMenuAdjustsNeeds() {
    test("cheat menu adjusts needs within safe bounds", [] {
        gtc::World world;
        const auto id = world.addCharacter("Cheat Subject", false);
        world.adjustNeed(id, "bladder", 100);
        world.adjustNeed(id, "bowel", 25);
        check(world.character(id).needs.bladder == 100.0F, "bladder can be filled and clamps at 100");
        const float bowel = world.character(id).needs.bowel;
        world.adjustNeed(id, "bladder", -150);
        world.adjustNeed(id, "invalid", 100);
        check(world.character(id).needs.bladder == 0.0F, "bladder can be emptied and clamps at zero");
        check(world.character(id).needs.bowel == bowel, "invalid need names are ignored");
    });
}

void runUnzippedFirstpersonPissDrains() {
    test("unzipped first-person piss drains bladder and makes puddles", [] {
        gtc::World world;
        const auto id = world.addCharacter("Postal Worker", false);
        check(!world.character(id).unzipped, "male citizens start zipped");
        world.setInput(id, {}, false, true, 0.0F, -0.2F);
        world.update(0.05F);
        check(!world.character(id).pissing, "zipped trousers block manual pissing");
        world.setInput(id, {}, false, false, 0.0F, -0.2F);
        world.toggleZip(id);
        check(world.character(id).unzipped && world.snapshot().find("\"unzipped\":true") != std::string::npos,
              "trouser toggle is authoritative and replicated");
        const float before = world.character(id).needs.bladder;
        world.setInput(id, {}, false, true, 0.0F, -0.2F);
        for (int i = 0; i < 20; ++i) world.update(0.05F);
        const auto after = world.character(id);
        check(after.pissing, "pissing state is active");
        check(after.needs.bladder < before - 3.4F && after.needs.bladder > before - 3.8F, "bladder drains");
        check(world.snapshot().find("\"puddles\":[{\"id\":") != std::string::npos,
              "urine creates persistent world puddles");
        world.setInput(id, {}, false, false, 0.0F, 0.0F);
        world.update(0.05F);
        check(!world.character(id).pissing, "releasing input stops pissing");
        world.toggleZip(id);
        check(!world.character(id).unzipped, "T restores the zipped default");
    });
}

void runPhysicalPissRisesAbove() {
    test("physical piss rises above head and splashes real surfaces", [] {
        using gtc::projectile_detail::Point;
        const gtc::Obstacle wall{{1, 0}, {0.1F, 2}, 2.0F, "boundary"};
        const auto wallHit = gtc::projectile_detail::sweepSurfaces({0, 1, 0}, {2, 1, 0}, 0.022F, {wall});
        check(wallHit.hit && wallHit.puddle.vertical && wallHit.puddle.normal.x < -0.9F &&
              wallHit.puddle.height > 0.9F, "swept piss leaves a vertical wall stain");
        const gtc::Obstacle crate{{1.4F, 0}, {0.6F, 0.6F}, 0.6F, "crate"};
        const auto topHit = gtc::projectile_detail::sweepSurfaces({1.4F, 1, 0}, {1.4F, 0.4F, 0}, 0.022F, {crate});
        check(topHit.hit && !topHit.puddle.vertical && std::abs(topHit.puddle.height - 0.608F) < 0.001F,
              "swept piss can stain the top of an object");
        const gtc::Obstacle toilet{{0, -4.75F}, {0.2285F, 0.3525F}, gtc::kToiletTankHeight, "toilet"};
        const auto bowlHit = gtc::projectile_detail::sweepSurfaces({0, 0.79F, -4.69F}, {0, 0.20F, -4.72F},
            0.022F, {toilet});
        check(bowlHit.hit && bowlHit.toilet && bowlHit.absorb,
              "the open toilet bowl absorbs descending piss below its real rim");
        std::vector<gtc::Puddle> spots;
        gtc::Puddle input;
        const auto first = gtc::placePuddle(spots, input, 1, {}, 9, 7);
        spots.push_back(first);
        const auto second = gtc::placePuddle(spots, input, 2, {}, 9, 7);
        check(first.id == 1 && second.id == 0 && spots.size() == 1 &&
              spots.front().size > first.size && gtc::length(spots.front().position) < 0.001F,
              "overlapping floor impacts merge into one growing puddle without displacement");
        gtc::Puddle separated; separated.position = {0.065F, 0};
        spots.push_back(gtc::placePuddle(spots, separated, 3, {}, 9, 7));
        gtc::placePuddle(spots, input, 4, {}, 9, 7);
        check(spots.size() == 1, "growth coalesces every newly overlapping floor puddle");
        for (std::uint32_t next = 5; next <= 220; ++next) gtc::placePuddle(spots, input, next, {}, 9, 7);
        const float at220 = spots.front().size;
        const float expected220 = gtc::puddleRadiusForVolume(220.0F * gtc::kPissDropletVolumeMl);
        check(spots.size() == 1 && std::abs(at220 - expected220) < 0.001F && at220 < 0.25F &&
              std::abs(spots.front().volumeMl - 220.0F * gtc::kPissDropletVolumeMl) < 0.001F,
              "220 physical droplets conserve their exact millilitre volume and derived area");
        for (std::uint32_t next = 221; next <= 840; ++next) gtc::placePuddle(spots, input, next, {}, 9, 7);
        const float expectedFull = gtc::puddleRadiusForVolume(gtc::kPissFullBladderVolumeMl);
        check(spots.size() == 1 && std::abs(spots.front().size - expectedFull) < 0.001F &&
              std::abs(spots.front().volumeMl - 600.0F) < 0.001F &&
              std::abs(expectedFull - 0.437F) < 0.002F && spots.front().size / at220 < 2.0F,
              "600 ml covers 0.6 square metres and four times the liquid gives twice the radius");

        gtc::World upward;
        const auto shooter = upward.addCharacter("Postal Arc", false);
        place(upward, shooter, {0, 0});
        upward.toggleZip(shooter);
        upward.setInput(shooter, {}, false, true, 0, 1.57079633F);
        float apex = 0, drift = 0;
        for (int i = 0; i < 10; ++i) {
            upward.update(0.05F);
            for (const auto& projectile : upward.projectiles()) {
                apex = std::max(apex, projectile.height);
                drift = std::max(drift, std::abs(projectile.position.x));
            }
        }
        check(apex > gtc::kStandingHeight && apex > 2.4F, "ninety-degree piss physically rises above the head");
        check(drift > 0.005F, "wind physically deflects an initially vertical stream");
        check(upward.snapshot().find("\"projectiles\":[{\"id\":") != std::string::npos,
              "actual projectile positions and velocities replicate");

        gtc::World world;
        const auto id = world.addCharacter("Wall Painter", false);
        place(world, id, {-7.5F, 2});
        world.toggleZip(id);
        world.adjustNeed(id, "bladder", 100);
        world.setInput(id, {}, false, true, -1.57079632679F, 0);
        for (int i = 0; i < 12; ++i) world.update(0.05F);
        check(world.snapshot().find("\"vertical\":true") != std::string::npos,
              "wall splash height and orientation replicate after physical impact");
        const auto wallSpots = world.puddles();
        const float largest = wallSpots.empty() ? 0.0F : std::max_element(wallSpots.begin(), wallSpots.end(),
            [](const gtc::Puddle& a, const gtc::Puddle& b) { return a.size < b.size; })->size;
        check(!wallSpots.empty() && wallSpots.size() <= 4 && largest > 0.025F,
              "repeated wall impacts grow compact stains instead of stacking transparent duplicates");
        for (int i = 12; i < 420; ++i) world.update(0.05F);
        check(world.puddles().size() <= 8 && world.snapshot().size() < 65536,
              "a full sustained wall stream stays compact and below the old fatal snapshot threshold");

        gtc::World aimed;
        const auto aimer = aimed.addCharacter("Porcelain Marksman", false);
        place(aimed, aimer, {0, -3.55F});
        aimed.toggleZip(aimer);
        aimed.adjustNeed(aimer, "bladder", 100);
        aimed.setInput(aimer, {}, false, true, 3.14159265F, -0.9F, false, false, false, false, false,
                       {0, -4.69F}, gtc::kToiletWaterHeight, true);
        bool emitted = false;
        const gtc::Vec2 aims[] = {{0, -4.60F}, {0, -4.69F}, {0, -4.78F}, {-0.055F, -4.69F}, {0.055F, -4.69F}};
        for (int i = 0; i < 30; ++i) {
            const gtc::Vec2 target = aims[i % 5];
            aimed.setInput(aimer, {}, false, true, 3.14159265F, -0.9F, false, false, false, false, false,
                           target, gtc::kToiletWaterHeight, true);
            aimed.update(0.05F);
            emitted = emitted || !aimed.projectiles().empty();
        }
        check(emitted && aimed.puddles().empty(),
              "crosshair-converged physical droplets enter the toilet water instead of falling short");
        const auto ripples = aimed.waterRipples();
        float rippleSpread = 0;
        bool exactContacts = !ripples.empty();
        for (const auto& a : ripples) for (const auto& b : ripples) rippleSpread = std::max(rippleSpread, distance(a.position, b.position));
        for (const auto& ripple : ripples) {
            bool atAim = false;
            for (const auto& aim : aims) atAim = atAim || distance(ripple.position, aim) < 0.025F;
            exactContacts = exactContacts && atAim;
        }
        for (const auto& aim : aims) {
            bool contacted = false;
            for (const auto& ripple : ripples) contacted = contacted || distance(ripple.position, aim) < 0.025F;
            exactContacts = exactContacts && contacted;
        }
        check(exactContacts && rippleSpread > 0.03F && aimed.snapshot().find("\"ripples\":[{\"id\":") != std::string::npos,
              "every physical ripple matches a requested aim point, including near and far water");

        gtc::World elevated;
        const auto painter = elevated.addCharacter("High Wall Marksman", false);
        place(elevated, painter, {-7.5F, 2});
        elevated.toggleZip(painter);
        elevated.adjustNeed(painter, "bladder", 100);
        elevated.setInput(painter, {}, false, true, -1.57079633F, 0.5F, false, false, false, false, false,
                          {-8.84F, 2}, 1.5F, true);
        for (int i = 0; i < 40; ++i) elevated.update(0.05F);
        bool accurate = false;
        for (const auto& puddle : elevated.puddles()) {
            accurate = accurate || (puddle.vertical && std::abs(puddle.height - 1.5F) < 0.08F);
        }
        check(accurate, "close-range aimed piss compensates gravity and strikes an elevated wall target");
    });
}

void runUnifiedFloorPuddlesAnd() {
    test("unified floor puddles and reactive hollow toilet water", [] {
        gtc::World floor;
        const auto id = floor.addCharacter("One Puddle", false);
        place(floor, id, {0, 0});
        floor.toggleZip(id);
        floor.adjustNeed(id, "bladder", 100);
        floor.setInput(id, {}, false, true, 0, 0, false, false, false, false, false,
                       {0, 1.0F}, 0.018F, true);
        for (int i = 0; i < 60; ++i) floor.update(0.05F);
        const auto spots = floor.puddles();
        check(spots.size() == 1 && spots.front().size > 0.12F && spots.front().size < 0.18F &&
              spots.front().volumeMl > 0.0F && floor.snapshot().find("\"ml\":") != std::string::npos,
              "a sustained stream grows one unified puddle and replicates its millilitres");

        gtc::World bullet;
        const auto shooter = bullet.addCharacter("Water Shooter", false);
        place(bullet, shooter, {0, -3.55F});
        bullet.toggleZip(shooter);
        bullet.attackAt(shooter, {0, -4.69F}, gtc::kToiletWaterHeight);
        for (int i = 0; i < 12; ++i) bullet.update(0.05F);
        check(!bullet.waterRipples().empty(), "an aimed physical bullet splashes the real water ellipse");

        gtc::World legs;
        const auto wader = legs.addCharacter("Bowl Wader", false);
        place(legs, wader, {0, -4.69F});
        for (int i = 0; i < 20 && gtc::distance(legs.character(wader).position, {0, -4.69F}) > 0.025F; ++i) {
            const gtc::Vec2 direction = gtc::normalized(gtc::Vec2{0, -4.69F} - legs.character(wader).position);
            legs.setInput(wader, direction, false, false, std::atan2(direction.x, direction.y), 0);
            legs.update(0.05F);
        }
        legs.setInput(wader, {}, false, false, 0, 0);
        for (int i = 0; i < 20; ++i) legs.update(0.05F);
        const auto settled = legs.character(wader);
        check(settled.elevation < 0.08F && settled.waterContact,
              "feet enter the hollow bowl instead of standing on an invisible seat plane");
        check(!legs.waterRipples().empty(), "legs entering and moving in water create ripple feedback");
    });
}

void runFifteentimesClockBladderCalibration() {
    test("fifteen-times clock, bladder calibration, and hourly evaporation", [] {
        gtc::World bladder;
        const auto id = bladder.addCharacter("Twenty One Seconds", false);
        bladder.toggleZip(id);
        bladder.adjustNeed(id, "bladder", 100);
        bladder.adjustNeed(id, "bladder", -25);
        bladder.setInput(id, {}, false, true, 0, 0);
        for (int i = 0; i < 419; ++i) bladder.update(0.05F);
        check(bladder.character(id).needs.bladder > 0.0F, "seventy-five percent still flows before twenty-one seconds");
        bladder.update(0.05F);
        check(bladder.character(id).needs.bladder == 0.0F, "seventy-five percent empties at twenty-one seconds");
        bladder.update(0.05F);
        check(!bladder.character(id).pissing, "the empty bladder stops emission");

        gtc::World wet;
        const auto wetId = wet.addCharacter("Persistent Spot", false);
        place(wet, wetId, {0, 0});
        wet.toggleZip(wetId);
        wet.setInput(wetId, {}, false, true, 0, -0.4F);
        for (int i = 0; i < 30; ++i) wet.update(0.05F);
        wet.setInput(wetId, {}, false, false, 0, 0);
        wet.update(0.05F);
        wet.removeCharacter(wetId);
        check(!wet.puddles().empty(), "physical impacts leave persistent stains");
        const double gameBefore = wet.gameSeconds();
        for (int i = 0; i < 100; ++i) wet.update(0.1F);
        check(std::abs((wet.gameSeconds() - gameBefore) - 150.0) < 0.001,
              "ten real seconds advance one hundred fifty game seconds");
        const auto beforeEvaporation = wet.puddles();
        for (int i = 0; i < 2400; ++i) wet.update(0.1F);
        const auto afterOneHour = wet.puddles();
        const double retention = std::exp(-gtc::kPissEvaporationMlPerSquareMeterGameHour *
            0.000001 / gtc::kPissFilmDepth);
        bool hourly = afterOneHour.size() == beforeEvaporation.size();
        for (std::size_t i = 0; hourly && i < afterOneHour.size(); ++i) {
            const double expectedMl = beforeEvaporation[i].volumeMl * retention;
            hourly = std::abs(afterOneHour[i].volumeMl - expectedMl) < 0.001F &&
                std::abs(afterOneHour[i].size - gtc::puddleRadiusForVolume(expectedMl)) < 0.0001F &&
                beforeEvaporation[i].volumeMl - afterOneHour[i].volumeMl > 0.0F;
        }
        check(hourly, "one game hour subtracts the calibrated millilitre flux and derives radius from remaining volume");
        for (int i = 0; i < 23 * 2400; ++i) wet.update(0.1F);
        check(!wet.puddles().empty(), "there is no arbitrary deletion at twenty-four game hours");
        int steps = 0;
        while (!wet.puddles().empty() && steps < 200000) { wet.update(0.1F); ++steps; }
        check(wet.puddles().empty(), "blobs eventually disappear only after evaporation shrinks them to a tiny remnant");
    });
}

} // namespace

void tests_needs() {
    runLowTunnelPreservesManually();
    runShittingDrainsBowelWithout();
    runMaleToiletPissIs();
    runCreatorValidatesAppearanceAnd();
    runCheatMenuAdjustsNeeds();
    runUnzippedFirstpersonPissDrains();
    runPhysicalPissRisesAbove();
    runUnifiedFloorPuddlesAnd();
    runFifteentimesClockBladderCalibration();
}
