// Blows, and the god paths that deal them.

#include "harness.h"

namespace {

void runGodAttacksAreAuthoritative() {
    test("god attacks are authoritative, ranged, and blockable", [] {
        gtc::World melee;
        const auto attacker = melee.addCharacter("Bonker", false);
        const auto target = melee.addCharacter("Target", false);
        place(melee, attacker, {0, 0});
        place(melee, target, {0, 1});
        melee.attack(attacker, target);
        check(melee.character(target).health == 88.0F && melee.character(attacker).attackAnim > 0.0F,
              "in-range melee applies damage and attack animation");
        melee.setInput(target, {}, false, false, 0, 0, false, false, false, false, true);
        for (int i = 0; i < 12; ++i) melee.update(0.05F);
        melee.attack(attacker, target);
        check(melee.character(target).blocking && melee.character(target).health > 83.7F &&
              melee.character(target).health < 84.0F, "melee RMB blocking reduces server damage");
        check(melee.snapshot().find("\"health\":") != std::string::npos &&
              melee.snapshot().find("\"blocking\":true") != std::string::npos,
              "combat health and block state replicate");

        gtc::World ranged;
        const auto pisser = ranged.addCharacter("Golden Gun", false);
        const auto victim = ranged.addCharacter("Dry Target", false);
        place(ranged, pisser, {0, 0});
        place(ranged, victim, {0, 2.5F});
        ranged.toggleZip(pisser);
        const float bladder = ranged.character(pisser).needs.bladder;
        ranged.attack(pisser, victim, true);
        const auto aimed = ranged.character(pisser);
        const auto aimedTarget = ranged.character(victim);
        const float expectedYaw = std::atan2(aimedTarget.position.x - aimed.position.x,
                                             aimedTarget.position.y - aimed.position.y);
        check(aimedTarget.health == 100.0F && aimed.needs.bladder < bladder && ranged.projectiles().size() == 1 &&
              std::abs(aimed.yaw - expectedYaw) < 0.0001F,
              "ranged click spawns a physical round instead of applying instant damage");
        check(ranged.character(pisser).path.empty() && ranged.character(pisser).attackAnim > 0.0F,
              "ranged attack never creates an approach path");
        {
            // The round must leave the meatus, not a point in front of the crotch.
            const auto shots = ranged.projectiles();
            const auto& shot = shots.front();
            const auto shooter = ranged.character(pisser);
            const float cp = std::cos(shooter.pitch), sp = std::sin(shooter.pitch);
            const gtc::Vec2 forward{std::sin(shooter.yaw), std::cos(shooter.yaw)};
            const gtc::Vec2 origin = shooter.position +
                forward * (gtc::kPissEmitterPivotForward + gtc::kPissEmitterShaft * cp);
            const float height = shooter.elevation + gtc::kPissEmitterPivotHeight + gtc::kPissEmitterShaft * sp;
            const float gap = std::hypot(shot.position.x - origin.x, shot.position.y - origin.y);
            check(gap < 1e-4F && std::abs(shot.height - height) < 1e-4F,
                  "ranged round leaves the anatomical meatus");
        }
        for (int i = 0; i < 20 && !ranged.projectiles().empty(); ++i) ranged.update(0.05F);
        check(ranged.character(victim).health == 93.0F, "the physical round damages only after swept impact");
        place(ranged, victim, {0, 4.0F});
        for (int i = 0; i < 12; ++i) ranged.update(0.05F);
        const float health = ranged.character(victim).health;
        const float attemptBladder = ranged.character(pisser).needs.bladder;
        ranged.attack(pisser, victim, true);
        const auto attempt = ranged.character(pisser);
        check(ranged.character(victim).health == health && attempt.path.empty() && attempt.attackAnim > 0.0F &&
              attempt.needs.bladder < attemptBladder && !ranged.projectiles().empty() &&
              attempt.notice.find("Physical shot") != std::string::npos,
              "long ranged attacks always spawn a round without chasing or refusing");
        for (int i = 0; i < 20 && !ranged.projectiles().empty(); ++i) ranged.update(0.05F);
        check(ranged.character(victim).health < health, "physical rounds have no artificial three-metre cutoff");

        gtc::World approach;
        const auto chaser = approach.addCharacter("Chaser", false);
        const auto runner = approach.addCharacter("Runner", false);
        place(approach, chaser, {0, 0});
        place(approach, runner, {0, 4});
        approach.attack(chaser, runner);
        check(approach.character(chaser).pendingAttack == runner && !approach.character(chaser).path.empty(),
              "single-left melee creates a go-there attack order");
        for (int i = 0; i < 240 && approach.character(chaser).pendingAttack != 0; ++i) approach.update(0.05F);
        check(approach.character(runner).health == 88.0F, "melee approach finishes with an authoritative hit");
    });
}

void runAdvancedGodPathsSprint() {
    test("advanced god paths sprint, crawl, jump, and climb", [] {
        gtc::World sprint;
        const auto runner = sprint.addCharacter("Auto Runner", false);
        place(sprint, runner, {0, 0});
        sprint.setDestination(runner, {0, 2}, true, false);
        sprint.update(0.05F);
        check(std::abs(sprint.character(runner).speed * 3.6F - 20.0F) < 0.001F,
              "Shift-RMB path uses the sprint speed");

        gtc::World crawl;
        const auto crawler = crawl.addCharacter("Advanced Crawler", false);
        place(crawl, crawler, {-7.5F, 0});
        crawl.setDestination(crawler, {-3.5F, 0}, false, true);
        bool sawProne = false;
        for (int i = 0; i < 500 && !crawl.character(crawler).path.empty(); ++i) {
            crawl.update(0.05F);
            sawProne = sawProne || crawl.character(crawler).prone;
        }
        check(sawProne && gtc::distance(crawl.character(crawler).position, {-3.5F, 0}) < 0.2F,
              "double-RMB automatically crawls through the low tunnel");

        gtc::World jump;
        const auto jumper = jump.addCharacter("Advanced Jumper", false);
        place(jump, jumper, {4.5F, 3.7F});
        jump.setDestination(jumper, {6.7F, 3.7F}, false, true);
        bool sawJump = false;
        for (int i = 0; i < 500 && !jump.character(jumper).path.empty(); ++i) {
            jump.update(0.05F);
            sawJump = sawJump || (!jump.character(jumper).grounded && jump.character(jumper).elevation > 0.15F);
        }
        check(sawJump && jump.character(jumper).elevation > 0.8F &&
              gtc::distance(jump.character(jumper).position, {6.7F, 3.7F}) < 0.2F,
              "double-RMB jumps onto the 85 centimetre crate");

        gtc::World climb;
        const auto climber = climb.addCharacter("Advanced Climber", false);
        place(climb, climber, {-8.4F, -3.8F});
        climb.setDestination(climber, {-6.6F, -3.8F}, false, true);
        bool sawGrab = false;
        for (int i = 0; i < 600 && !climb.character(climber).path.empty(); ++i) {
            climb.update(0.05F);
            sawGrab = sawGrab || climb.character(climber).grabbing;
        }
        check(sawGrab && climb.character(climber).elevation > 1.2F &&
              gtc::distance(climb.character(climber).position, {-6.6F, -3.8F}) < 0.2F,
              "double-RMB climbs the 1.25 metre crate");
    });
}

void runAIUsesTheSame() {
    test("AI uses the same multi-action toilet system", [] {
        gtc::World world;
        const auto id = world.addCharacter("Automated Dump", true);
        bool usedToilet = false;
        for (int i = 0; i < 800; ++i) {
            world.update(0.05F);
            if (world.character(id).toiletAction != gtc::ToiletAction::None) {
                usedToilet = true;
                break;
            }
        }
        check(usedToilet, "AI should autonomously request and use a toilet action");
    });
}

} // namespace

void tests_combat() {
    runGodAttacksAreAuthoritative();
    runAdvancedGodPathsSprint();
    runAIUsesTheSame();
}
