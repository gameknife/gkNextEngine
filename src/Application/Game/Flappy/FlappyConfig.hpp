#pragma once

#include "FlappyCommon.hpp"

namespace Flappy
{
    FGameplayConfig LoadGameplayConfig(const std::string& path = "assets/projects/Flappy/Content/configs/gameplay.json");
    FReplayConfig LoadReplayConfig(const std::string& path = "assets/projects/Flappy/Content/configs/replay.json");
}
