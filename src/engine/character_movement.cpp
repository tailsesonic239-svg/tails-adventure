#include "character.h"
#include "controller.h"
#include "tools.h"

void TA_Character::physicsStep() {
    if(!hurt) {
        if(sonicHoming) {
            updateSonicHoming();
        } else if(sonicCharging) {
            updateSonicCharge();
        } else if(sonicDashing) {
            updateSonicDash();
        } else if(water && (!TA::equal(windVelocity.x, 0) || !TA::equal(windVelocity.y, 0))) {
            updateWaterFlow();
        } else if(helitail) {
            updateHelitail();
        } else if(ground) {
            updateGround();
        } else {
            updateAir();
        }
    } else {
        if(water && (!TA::equal(windVelocity.x, 0) || !TA::equal(windVelocity.y, 0))) {
            hurt = false;
        }
        velocity.y += grv * (water ? 0.5F : 1) * TA::elapsedTime;
        velocity.y = std::min(velocity.y, maxJumpSpeed * (water ? 0.5F : 1));
    }
}

void TA_Character::initHelitail() {
    helitail = true;
    helitailTime = 0;
    if(ground) {
        velocity.y = -2;
    }
    if(remoteRobot) {
        setAnimation("remote_robot_fly_init");
    }
}

void TA_Character::updateGround() {
    horizontalMove();
    jump = spring = false;
    coyoteTime = 0;
    lookUp = (links.controller->getDirection() == TA_DIRECTION_UP);
    crouch = (links.controller->getDirection() == TA_DIRECTION_DOWN);
    if(lookUp || crouch) {
        velocity.x = 0;
    }
    if(links.controller->isJustPressed(TA_BUTTON_A)) {
        if(crouch && characterId == TA_CHARACTER_SONIC && !water) {
            sonicCharging = true;
            sonicChargeCount = 0;
            velocity.x = 0;
            attackHitbox.setRectangle(TA_Point(6, 20), TA_Point(34, 39));
            hammerSound.play();
        } else if(lookUp && characterId != TA_CHARACTER_SONIC && (!water || remoteRobot)) {
            initHelitail();
            ground = false;
        } else {
            jumpSound.play();
            jumpSpeed = (remoteRobot ? remoteRobotJumpSpeed : jmp) * (water ? 0.7F : 1);
            jump = true;
            jumpReleased = false;
            jumpTime = 0;
            updateAir();
            ground = false;
        }
    }
}

void TA_Character::updateSonicHoming() {
    sonicHomingTime += TA::elapsedTime;
    attackHitbox.setPosition(position);

    TA_Point toTarget = sonicHomingTarget - position;
    float distance = toTarget.length();

    if(distance < 8 || sonicHomingTime > sonicHomingMaxTime || ground) {
        sonicHoming = false;
        jump = true;
        jumpReleased = true;
        jumpSpeed = sonicHomingBounceSpeed;
        velocity.x = 0;
        velocity.y = sonicHomingBounceSpeed;
        return;
    }

    velocity = toTarget * (sonicHomingSpeed / distance);
    flip = (velocity.x < 0);
}

void TA_Character::updateSonicCharge() {
    if(!ground) {
        sonicCharging = false;
        updateAir();
        return;
    }

    jump = spring = false;
    coyoteTime = 0;
    crouch = (links.controller->getDirection() == TA_DIRECTION_DOWN);
    velocity.x = 0; // fica travado enquanto carrega

    attackHitbox.setPosition(position);

    if(links.controller->isJustPressed(TA_BUTTON_A) && sonicChargeCount < sonicDashMaxCharge) {
        sonicChargeCount++;
        hammerSound.play();
    }

    if(!crouch) {
        sonicCharging = false;
        sonicDashing = true;
        sonicDashTime = 0;
        sonicDashWallFrames = 0;
        float speed = sonicDashBaseSpeed + sonicDashChargeStep * sonicChargeCount;
        velocity.x = (flip ? -speed : speed);
        jumpSound.play();
    }
}

void TA_Character::updateSonicDash() {
    sonicDashTime += TA::elapsedTime;

    if(sonicDashTime > sonicDashMaxTime) {
        const float dashSlowdown = 0.05F;
        if(velocity.x > 0) {
            velocity.x = std::max(0.0F, velocity.x - dashSlowdown * TA::elapsedTime);
        } else {
            velocity.x = std::min(0.0F, velocity.x + dashSlowdown * TA::elapsedTime);
        }
        if(std::abs(velocity.x) < 0.05F) {
            sonicDashing = false;
        }
    }

    if(!ground) {
        velocity.y += grv * TA::elapsedTime;
        velocity.y = std::min(velocity.y, maxJumpSpeed);
    } else if(links.controller->isJustPressed(TA_BUTTON_A)) {
        sonicDashing = false;
        jumpSound.play();
        jumpSpeed = jmp;
        jump = true;
        jumpReleased = false;
        jumpTime = 0;
        ground = false;
        attackHitbox.setPosition(position);
        return;
    }

    attackHitbox.setPosition(position);

    if(wall) {
        sonicDashWallFrames++;
        if(sonicDashWallFrames >= 2) {
            sonicDashing = false;
        }
    } else {
        sonicDashWallFrames = 0;
    }
}

void TA_Character::updateAir() {
    coyoteTime += TA::elapsedTime;
    horizontalMove();

    if(jump) {
        jumpSpeed += grv * (water ? 0.5F : 1) * TA::elapsedTime;
        jumpSpeed = std::min(jumpSpeed, maxJumpSpeed);
        jumpTime += TA::elapsedTime;

        if(jump && !jumpReleased && !links.controller->isPressed(TA_BUTTON_A)) {
            jumpReleased = true;
        }
        if(jumpReleased && !spring) {
            jumpSpeed = std::max(jumpSpeed, releaseJumpSpeed);
        }

        if(water && jumpSpeed > maxJumpSpeed * 0.5F) {
            jumpSpeed = std::max(maxJumpSpeed * 0.5F, jumpSpeed - waterFriction * TA::elapsedTime);
        }
        if(spring) {
            velocity.y = std::min(maxJumpSpeed * (water ? 0.5F : 1), jumpSpeed);
        } else {
            velocity.y = std::min(maxJumpSpeed, std::max(minJumpSpeed * (water ? 0.5F : 1), jumpSpeed));
        }
        if(jump && jumpReleased && (!water || remoteRobot) && links.controller->isJustPressed(TA_BUTTON_A)) {
            if(characterId == TA_CHARACTER_SONIC) {
                TA_Point targetPos;
                if(links.objectSet->findNearestTarget(position, sonicHomingRange, targetPos)) {
                    sonicHoming = true;
                    sonicHomingTime = 0;
                    sonicHomingTarget = targetPos;
                    attackHitbox.setRectangle(TA_Point(6, 20), TA_Point(34, 39));
                    hammerSound.play();
                }
            } else {
                initHelitail();
            }
        }
    }

    else {
        velocity.y += grv * (water ? 0.5F : 1) * TA::elapsedTime;
        velocity.y = std::min(velocity.y, maxJumpSpeed);
        if(water && velocity.y > maxJumpSpeed * 0.5F) {
            velocity.y = std::max(maxJumpSpeed * 0.5F, velocity.y - waterFriction * TA::elapsedTime);
        }

        if(links.controller->isJustPressed(TA_BUTTON_A) && coyoteTime < maxCoyoteTime) {
            jumpSound.play();
            jumpSpeed = (remoteRobot ? remoteRobotJumpSpeed : jmp) * (water ? 0.7 : 1);
            jump = true;
            jumpReleased = false;
            jumpTime = 0;
        }
    }
}

void TA_Character::updateHelitail() {
    auto process = [&](float& x, float need) {
        if(x > need) {
            x = std::max(need, x - helitailAcc * TA::elapsedTime);
        } else {
            x = std::min(need, x + helitailAcc * TA::elapsedTime);
        }
    };

    helitailTime += TA::elapsedTime;
    TA_Point vector;
    TA_Direction direction = links.controller->getDirection();
    if(direction != TA_DIRECTION_MAX) {
        vector = links.controller->getDirectionVector();
    }
    if(!remoteRobot && helitailTime > getMaxHelitailTime()) {
        vector.y = 1;
    }

    if(direction == TA_DIRECTION_LEFT) {
        flip = true;
    } else if(direction == TA_DIRECTION_RIGHT) {
        flip = false;
    }

    float topSpeed = helitailTop * (water ? 0.7 : 1);
    process(velocity.x, vector.x * topSpeed);
    process(velocity.y, vector.y * topSpeed);

    if(links.controller->isJustPressed(TA_BUTTON_A) || (water && !remoteRobot)) {
        jump = helitail = false;
    }

    if(!TA::sound::isPlaying(TA_SOUND_CHANNEL_SFX1)) {
        if(remoteRobot) {
            remoteRobotFlySound.play();
        } else {
            flySound.play();
        }
    }
}

void TA_Character::horizontalMove() {
    TA_Direction direction = links.controller->getDirection();
    if(remoteRobot && ground && (direction == TA_DIRECTION_LEFT || direction == TA_DIRECTION_RIGHT) &&
        !TA::sound::isPlaying(TA_SOUND_CHANNEL_SFX1)) {
        remoteRobotStepSound.play();
    }

    float currentTopX = topX;
    float currentAcc = (ground ? acc : airAcc);

    if(usingSpeedBoots) {
        currentTopX *= 2;
    }
    if(water) {
        currentTopX *= 0.75;
        currentAcc *= 0.5;
    }

    if(direction == TA_DIRECTION_RIGHT) {
        flip = false;
        velocity.x += currentAcc * TA::elapsedTime;
        velocity.x = std::min(velocity.x, currentTopX);
    } else if(direction == TA_DIRECTION_LEFT) {
        flip = true;
        velocity.x -= currentAcc * TA::elapsedTime;
        velocity.x = std::max(velocity.x, -currentTopX);
    } else {
        if(velocity.x > 0) {
            velocity.x = std::max(float(0), velocity.x - currentAcc * TA::elapsedTime);
        } else {
            velocity.x = std::min(float(0), velocity.x + currentAcc * TA::elapsedTime);
        }
    }
}

void TA_Character::updateWaterFlow() {
    auto addAcceleration = [&](float& speed, float neededSpeed) {
        if(speed < neededSpeed) {
            speed = std::min(neededSpeed, speed + waterFlowAcc * TA::elapsedTime);
        } else {
            speed = std::max(neededSpeed, speed - waterFlowAcc * TA::elapsedTime);
        }
    };

    coyoteTime = maxCoyoteTime + 1;
    addAcceleration(velocity.y, windVelocity.y);
    if(TA::equal(windVelocity.x, 0)) {
        horizontalMove();
    } else {
        addAcceleration(velocity.x, windVelocity.x);
    }
}
