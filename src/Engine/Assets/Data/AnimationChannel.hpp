#pragma once

#include <type_traits>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Assets
{
    template <typename T>
    struct AnimationKey
    {
        float Time;
        T Value;
    };

    template <typename T>
    struct AnimationChannel
    {
        std::vector<AnimationKey<T>> Keys;

        T Sample(float time) const
        {
            if (Keys.empty()) return T{};
            if (time < Keys.front().Time || Keys.size() == 1) return Keys.front().Value;
            for (size_t i = 1; i < Keys.size(); ++i)
            {
                const auto& next = Keys[i];
                if (!(time < next.Time)) continue;
                const auto& current = Keys[i - 1];
                const float interval = next.Time - current.Time;
                if (interval <= 1.0e-8f) return next.Value;
                const float alpha = (time - current.Time) / interval;
                if constexpr (std::is_same_v<T, glm::quat>)
                    return glm::slerp(current.Value, next.Value, alpha);
                else
                    return glm::mix(current.Value, next.Value, alpha);
            }
            return Keys.back().Value;
        }
    };
}
