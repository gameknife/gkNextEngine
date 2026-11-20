#include "Engine/Common/CoreMinimal.hpp"
#include "Gameplay/Character/NextCharacterController.h"

#include "Engine/Runtime/Subsystems/NextPhysics.hpp"

void NextCharacterController::Create(NextPhysics* physics, const FCharacterControllerSettings& settings)
{
    if (physics)
    {
        Create([physics](const FCharacterControllerSettings& spec) { return physics->CreateCharacterController(spec); },
               [physics] { physics->CompleteTick(); }, settings);
    }
    else
    {
        Create({}, {}, settings);
    }
}
