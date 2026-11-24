#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Interface/UiHost.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace NextUI
{
    class FTextureCache final
    {
    public:
        Runtime::FUiTextureHandle Request(std::string_view path,
                                          bool srgb,
                                          Runtime::EUiTextureLifetime lifetime,
                                          const std::function<uint64_t(std::string_view)>& resolveByName);

    private:
        std::unordered_set<std::string> loadRequests_;
        std::unordered_map<std::string, glm::vec2> pixelSizeCache_;
    };
}
