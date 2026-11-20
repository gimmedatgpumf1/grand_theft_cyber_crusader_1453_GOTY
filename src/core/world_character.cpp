#include "core/world.h"

#include "core/collision.h"
#include "core/world_internal.h"

#include <algorithm>
#include <cmath>

namespace gtc {

using namespace world_detail;

void World::updateCharacter(Character& character, float dt) {
    character.attackAnim = std::max(0.0F, character.attackAnim - dt);
    character.attackCooldown = std::max(0.0F, character.attackCooldown - dt);
    if (character.toiletAction != ToiletAction::None) {
        character.actionTime += dt;
        character.speed = 0.0F;
        character.wantsPiss = false;
        character.position = character.onToilet ? toiletSeat() : toiletFront();
        if (character.toiletAction == ToiletAction::Sit) {
            character.needs.bladder = std::min(100.0F, character.needs.bladder + 0.065F * dt);
            character.needs.bowel = std::min(100.0F, character.needs.bowel + 0.042F * dt);
            return;
        }
        if (character.toiletAction == ToiletAction::Shit) {
            character.pissing = false;
            character.needs.bowel = std::max(0.0F, character.needs.bowel - 25.0F * dt);
            if (character.needs.bowel < 0.1F || character.actionTime > 6.0F) {
                character.needs.bowel = 0.0F;
                addEvent(character.name + " flushed two generations of evidence.");
                finishToilet(character, "Bowel emptied. Bloodline unfortunately intact.");
            }
            return;
        }
        updatePissing(character, dt);
        if (character.needs.bladder <= 0.0F || character.actionTime > 30.0F) {
            character.needs.bladder = 0.0F;
            addEvent(character.name + " finished irrigation without painting the floor.");
            finishToilet(character, "Bladder emptied. Floor remains miraculously dry.");
        }
        return;
    }

    if (!character.wantsPiss || !(character.unzipped || character.ai)) {
        character.needs.bladder = std::min(100.0F, character.needs.bladder + 0.065F * dt);
    }
    character.needs.bowel = std::min(100.0F, character.needs.bowel + 0.042F * dt);
    const bool manualMovement = length(character.manualMove) > 0.05F;
    Vec2 direction = character.manualMove;
    if (manualMovement) {
        character.path.clear();
        character.pendingToilet = false;
        character.pendingAttack = 0;
        character.pathSprint = character.pathAdvanced = false;
    } else if (!character.path.empty()) {
        const Vec2 difference = character.path.front() - character.position;
        if (length(difference) < 0.18F) {
            character.path.erase(character.path.begin());
        }
        if (!character.path.empty()) direction = normalized(character.path.front() - character.position);
    }

    const bool moving = length(direction) > 0.05F;
    const bool jumpPressed = character.jumpInput && !character.jumpHeld;
    character.jumpHeld = character.jumpInput;
    const bool grabHeld = character.grabInput || (character.pathAdvanced && !character.path.empty());
    applyStance(character, character.position, obstacles_);
    if (!grabHeld) {
        character.grabbing = false;
        character.grabbedObstacle = -1;
        character.grabHeight = 0.0F;
        character.charge = character.fullChargeTime = 0.0F;
        character.chargeLocked = false;
    }
    const bool proneInput = character.grounded && character.crouchInput && character.grabInput;
    if (character.grabInput && !character.grabbing && !character.chargeLocked && !proneInput) {
        const int target = grabTarget(character, obstacles_);
        if (target >= 0) {
            character.grabbing = true;
            character.grabbedObstacle = target;
            character.grabHeight = obstacleHeightAt(obstacles_[target], character.position);
            character.grounded = false;
            character.verticalVelocity = character.charge = character.fullChargeTime = 0.0F;
        }
    }
    if (character.grabbing) {
        const Obstacle* obstacle = character.grabbedObstacle >= 0 &&
            static_cast<std::size_t>(character.grabbedObstacle) < obstacles_.size() ?
            &obstacles_[character.grabbedObstacle] : nullptr;
        if (!obstacle || !grabHeld) {
            character.grabbing = false;
            character.grabbedObstacle = -1;
            character.grabHeight = 0.0F;
        } else {
            character.speed = character.verticalVelocity = character.charge = character.fullChargeTime = 0.0F;
            const Vec2 forward{std::sin(character.yaw), std::cos(character.yaw)};
            float climb = character.manualMove.x * forward.x + character.manualMove.y * forward.y;
            if (!manualMovement && character.pathAdvanced) climb = direction.x * forward.x + direction.y * forward.y;
            if (climb < -0.2F) {
                character.grabbing = false;
                character.grabbedObstacle = -1;
                character.grabHeight = 0.0F;
                character.chargeLocked = true;
            } else if (climb > 0.2F) {
                character.elevation += kClimbSpeed * dt;
                character.speed = kCrouchSpeed;
                if (character.elevation >= character.grabHeight) {
                    character.elevation = character.grabHeight;
                    character.grabbing = false;
                    character.grabbedObstacle = -1;
                    character.grabHeight = 0.0F;
                    character.grounded = true;
                    character.chargeLocked = true;
                    const Vec2 candidate = character.position + forward * (kBodyRadius + 0.12F);
                    if (!bodyHitsWorld(candidate, kBodyRadius, character.elevation, bodyHeight(character),
                                       obstacles_, kHalfWidth, kHalfDepth)) character.position = candidate;
                }
            }
            if (character.grabbing) {
                updatePissing(character, dt);
                return;
            }
        }
    }
    applyStance(character, character.position, obstacles_);
    if (moving) {
        const bool prone = character.prone, crouched = character.crouched;
        applyStance(character, character.position + normalized(direction) * 0.45F, obstacles_);
        character.prone = character.prone || prone;
        character.crouched = !character.prone && (character.crouched || crouched);
    }
    if (jumpPressed && character.grounded && !character.prone) {
        const float charge = character.grabInput && !character.chargeLocked ? character.charge : 0.0F;
        const float height = kJumpHeight + (kMaximumJumpHeight - kJumpHeight) * charge;
        character.verticalVelocity = std::sqrt(2.0F * kGravity * height);
        character.grounded = false;
        character.charge = character.fullChargeTime = 0.0F;
    }
    if (character.grabInput && character.grounded && !character.grabbing &&
        !character.prone && !character.chargeLocked) {
        character.charge = std::min(1.0F, character.charge + dt / 3.0F);
        if (character.charge >= 1.0F) {
            character.fullChargeTime += dt;
            if (character.fullChargeTime >= 1.9999F) {
                character.charge = character.fullChargeTime = 0.0F;
                character.chargeLocked = true;
            }
        }
    }
    const float walkSpeed = character.ai ? (character.pendingToilet ? kFastWalkSpeed : kSlowWalkSpeed) :
        (character.slowWalk ? kSlowWalkSpeed : kFastWalkSpeed);
    const bool sprintPath = !manualMovement && character.pathSprint;
    character.speed = moving ? (character.sprint || sprintPath ? kSprintSpeed : walkSpeed) : 0.0F;
    if (character.prone) character.speed = moving ? kProneSpeed : 0.0F;
    else if (character.crouched) character.speed = moving ? kCrouchSpeed : 0.0F;
    if (moving && character.grounded) {
        const Vec2 probe = character.position + normalized(direction) * (kBodyRadius + character.speed * dt);
        for (const Obstacle& obstacle : obstacles_) {
            const float rise = obstacleHeightAt(obstacle, probe) - character.elevation;
            if (obstacle.kind != "boundary" && rise >= kAutomaticStepHeight - 0.0001F && rise <= kJumpHeight &&
                obstacle.base <= character.elevation + 0.02F &&
                bodyHitsObstacle(probe, kBodyRadius, character.elevation, bodyHeight(character), obstacle)) {
                character.speed = std::min(character.speed, 0.75F);
            }
        }
    }
    if (moving) {
        moveBody(character, normalized(direction) * (character.speed * dt));
        if (!character.wantsPiss && !manualMovement) character.yaw = std::atan2(direction.x, direction.y);
    }
    updateVertical(character, dt);
    const Vec2 waterLocal = character.position - toiletPosition() - Vec2{0.0F, kToiletBowlOffset};
    const float waterMetric = waterLocal.x * waterLocal.x / (kToiletWater.x * kToiletWater.x) +
        waterLocal.y * waterLocal.y / (kToiletWater.y * kToiletWater.y);
    const bool inWater = waterMetric <= 1.0F && character.elevation < kToiletWaterHeight;
    character.waterRippleTimer = std::max(0.0F, character.waterRippleTimer - dt);
    if (inWater && character.waterRippleTimer <= 0.0F && (moving || !character.waterContact)) {
        const float scale = waterMetric > 0.64F ? 0.8F / std::sqrt(waterMetric) : 1.0F;
        rippleWater(toiletPosition() + Vec2{0.0F, kToiletBowlOffset} + waterLocal * scale, moving ? 0.8F : 1.1F);
        character.waterRippleTimer = 0.22F;
    }
    character.waterContact = inWater;
    updatePissing(character, dt);
    updatePendingAttack(character);

    if (character.pendingToilet && character.path.empty()) {
        if (distance(character.position, toiletFront()) <= kInteractionRange && toiletUser_ == 0) {
            beginToilet(character, character.queuedToiletAction);
        } else if (toiletUser_ != 0) {
            character.speed = 0.0F;
        } else {
            character.pendingToilet = false;
            character.queuedToiletAction = ToiletAction::None;
            notify(character, "Stopped just outside throning range. Click it again.");
        }
    }
}

}  // namespace gtc
