#pragma once

#include "Engine/Runtime/Subsystems/CharacterPhysics.hpp"

namespace JPH
{
    class PhysicsSystem;
    class TempAllocator;
}

namespace Modules::Physics
{
    std::unique_ptr<INextCharacterControllerBackend> MakeJoltCharacter(
        JPH::PhysicsSystem& physicsSystem, JPH::TempAllocator& tempAllocator,
        const FCharacterControllerSettings& settings);
}
