#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Campaign.hpp"

namespace Assets { class Model; struct FMaterial; class Scene; class Node; }

namespace NextDayz
{
    class RescueHelicopter
    {
    public:
        void InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials);
        void OnSceneLoaded(Assets::Scene& scene);
        void Clear() { nodes_.clear(); }
        void Update(Assets::Scene& scene, const FFlightState& flight, const FExtractionConfig& config, float dt);
        glm::vec3 Position(const FFlightState& flight, const FExtractionConfig& config) const;

    private:
        struct FPart { glm::vec3 position; glm::vec3 scale; int material; bool round = false; bool rotor = false; };
        std::vector<FPart> parts_;
        std::vector<std::shared_ptr<Assets::Node>> nodes_;
        uint32_t cube_ = 0;
        uint32_t sphere_ = 0;
        std::array<uint32_t, 5> materials_{};
        float rotorAngle_ = 0.0f;
        bool active_ = false;
    };
}
