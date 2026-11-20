#pragma once

#include <bit>
#include <cstddef>
#include <limits>

namespace NextDayz::Net
{
    // Protocol 5 serializes these fields at fixed offsets on every supported host.
    static_assert(std::endian::native == std::endian::little);
    static_assert(sizeof(float) == 4 && sizeof(double) == 8 && std::numeric_limits<float>::is_iec559);
    static_assert(sizeof(bool) == 1 && sizeof(glm::vec3) == 12 && offsetof(glm::vec3, z) == 8);
    static_assert(offsetof(glm::vec3, x) == 0 && offsetof(glm::vec3, y) == 4);
    static_assert(sizeof(FGridSize) == 8 && offsetof(FGridSize, width) == 0 && offsetof(FGridSize, height) == 4);
    static_assert(sizeof(FGridPlacement) == 12 && offsetof(FGridPlacement, x) == 0 &&
                  offsetof(FGridPlacement, y) == 4 && offsetof(FGridPlacement, rotated) == 8);
    static_assert(sizeof(FWeaponAttachments) == 5 && sizeof(EWeaponAttachment) == 1);
    static_assert(sizeof(FZombieHandle) == 8 && offsetof(FZombieHandle, index) == 0 && offsetof(FZombieHandle, generation) == 4);
    static_assert(sizeof(FSurvivorPresence) == 48 && offsetof(FSurvivorPresence, id) == 0 &&
                  offsetof(FSurvivorPresence, feet) == 4 && offsetof(FSurvivorPresence, eye) == 16 &&
                  offsetof(FSurvivorPresence, forward) == 28 && offsetof(FSurvivorPresence, alive) == 40 &&
                  offsetof(FSurvivorPresence, sightScale) == 44);
    static_assert(sizeof(FPlayerLocomotionState) == 96 && offsetof(FPlayerLocomotionState, desiredStance) == 0 &&
                  offsetof(FPlayerLocomotionState, actualStance) == 4 && offsetof(FPlayerLocomotionState, gait) == 8 &&
                  offsetof(FPlayerLocomotionState, localMove) == 12 && offsetof(FPlayerLocomotionState, worldVelocity) == 20 &&
                  offsetof(FPlayerLocomotionState, horizontalSpeed) == 32 && offsetof(FPlayerLocomotionState, jumpPhase) == 36 &&
                  offsetof(FPlayerLocomotionState, jumpPhaseTime01) == 40 && offsetof(FPlayerLocomotionState, traversalAction) == 44 &&
                  offsetof(FPlayerLocomotionState, traversalTime01) == 48 && offsetof(FPlayerLocomotionState, traversalHeight) == 52 &&
                  offsetof(FPlayerLocomotionState, traversalYaw) == 56 && offsetof(FPlayerLocomotionState, traversalHand) == 60 &&
                  offsetof(FPlayerLocomotionState, traversalHandWeight) == 84 && offsetof(FPlayerLocomotionState, onGround) == 92 &&
                  offsetof(FPlayerLocomotionState, standBlocked) == 93);
    static_assert(sizeof(TimeSystem::FState) == 32 && offsetof(TimeSystem::FState, minutes) == 0 &&
                  offsetof(TimeSystem::FState, nextWeatherChange) == 8 && offsetof(TimeSystem::FState, weatherRng) == 16 &&
                  offsetof(TimeSystem::FState, weather) == 24 && offsetof(TimeSystem::FState, blend) == 28);
    static_assert(sizeof(FDamageEvent) == 56 && offsetof(FDamageEvent, sequence) == 0 && offsetof(FDamageEvent, target) == 8 &&
                  offsetof(FDamageEvent, hitZone) == 16 && offsetof(FDamageEvent, amount) == 20 && offsetof(FDamageEvent, impact) == 24 &&
                  offsetof(FDamageEvent, worldPoint) == 28 && offsetof(FDamageEvent, direction) == 40 &&
                  offsetof(FDamageEvent, killed) == 52 && offsetof(FDamageEvent, staggered) == 53 && offsetof(FDamageEvent, crippled) == 54);
}
