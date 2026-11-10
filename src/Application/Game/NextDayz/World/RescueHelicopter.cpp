#include "Engine/Common/CoreMinimal.hpp"
#include "RescueHelicopter.hpp"

#include <glm/gtc/quaternion.hpp>
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"

namespace NextDayz
{
    void RescueHelicopter::InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials)
    {
        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.5f), glm::vec3(0.5f)));
        cube_ = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 0.5f));
        sphere_ = static_cast<uint32_t>(models.size() - 1);
        materials_[0] = Assets::SceneBuilder::AddLambertianMaterial(materials, {0.26f, 0.32f, 0.23f});
        materials_[1] = Assets::SceneBuilder::AddLambertianMaterial(materials, {0.08f, 0.15f, 0.19f});
        materials_[2] = Assets::SceneBuilder::AddLambertianMaterial(materials, {0.12f, 0.13f, 0.12f});
        materials_[3] = Assets::SceneBuilder::AddLambertianMaterial(materials, {0.93f, 0.75f, 0.12f});
        materials_[4] = Assets::SceneBuilder::AddDiffuseLightMaterial(materials, {1.0f, 0.12f, 0.02f}, 250.0f);
        parts_ = {
            {{0,1.65f,0}, {2.65f,2.1f,4.6f}, 0, true},
            {{0,1.85f,-1.9f}, {2.2f,1.5f,1.9f}, 1, true},
            {{0,2.05f,3.35f}, {0.55f,0.6f,4.8f}, 0},
            {{0,2.9f,5.6f}, {0.12f,2.0f,1.0f}, 0},
            {{0,2.25f,4.8f}, {2.7f,0.12f,0.6f}, 0},
            {{0,2.8f,0.1f}, {1.3f,0.7f,1.8f}, 0, true},
            {{0,3.25f,0}, {0.22f,0.6f,0.22f}, 2},
            {{0,3.5f,0}, {10.6f,0.06f,0.24f}, 2, false, true},
            {{0,3.5f,0}, {0.24f,0.06f,10.6f}, 2, false, true},
            {{-1.35f,0.15f,0}, {0.13f,0.13f,5.0f}, 2},
            {{1.35f,0.15f,0}, {0.13f,0.13f,5.0f}, 2},
            {{-1.2f,0.65f,-1.2f}, {0.12f,1.0f,0.12f}, 2},
            {{1.2f,0.65f,-1.2f}, {0.12f,1.0f,0.12f}, 2},
            {{-1.2f,0.65f,1.2f}, {0.12f,1.0f,0.12f}, 2},
            {{1.2f,0.65f,1.2f}, {0.12f,1.0f,0.12f}, 2},
            {{1.29f,1.5f,0.2f}, {0.08f,1.4f,1.5f}, 1},
            {{1.35f,0.7f,0.2f}, {0.5f,0.12f,1.6f}, 3},
            {{-1.3f,1.6f,0.2f}, {0.05f,0.25f,2.5f}, 3},
            {{0,3.95f,5.6f}, {0.16f,0.16f,0.16f}, 4, true},
            // The fired signal rises above the helipad during the first six seconds.
            {{0,0,0}, {0.18f,0.7f,0.18f}, 4, true}
        };
    }

    void RescueHelicopter::OnSceneLoaded(Assets::Scene& scene)
    {
        nodes_.clear();
        active_ = false;
        for (const auto& part : parts_)
        {
            auto node = Assets::SceneBuilder::CreateRenderNode("nd_rescue_helicopter", {0,-1000,0}, part.scale,
                scene.GenerateInstanceId(), part.round ? sphere_ : cube_, materials_[part.material], false,
                glm::quat(1,0,0,0), false);
            auto physics = std::make_shared<Runtime::PhysicsComponent>();
            physics->SetMobility(Runtime::ENodeMobility::Dynamic);
            node->AddComponent(physics);
            scene.AddNode(node);
            nodes_.push_back(std::move(node));
        }
    }

    glm::vec3 RescueHelicopter::Position(const FFlightState& flight, const FExtractionConfig& config) const
    {
        glm::vec3 offset(0.0f);
        if (flight.phase == EFlightPhase::Inbound)
        {
            const float t = glm::clamp(flight.remaining / config.arrivalSeconds, 0.0f, 1.0f);
            offset = {t * 180.0f, 24.0f + t * 55.0f, t * 180.0f};
        }
        else if (flight.phase == EFlightPhase::Landing)
        {
            const float t = glm::clamp(flight.remaining / config.landingSeconds, 0.0f, 1.0f);
            offset.y = 24.0f * t * t * (3.0f - 2.0f * t);
        }
        else if (flight.phase == EFlightPhase::Departing)
        {
            const float t = 1.0f - glm::clamp(flight.remaining / config.departureSeconds, 0.0f, 1.0f);
            offset = {t * t * 20.0f, t * t * 45.0f, -t * t * 40.0f};
        }
        return flight.pad + offset;
    }

    void RescueHelicopter::Update(Assets::Scene& scene, const FFlightState& flight,
                                 const FExtractionConfig& config, float dt)
    {
        if (flight.phase == EFlightPhase::Idle && !active_) return;
        active_ = flight.phase != EFlightPhase::Idle;
        rotorAngle_ = std::fmod(rotorAngle_ + dt * 24.0f, 6.2831853f);
        const glm::vec3 position = Position(flight, config);
        for (size_t index = 0; index < nodes_.size(); ++index)
        {
            const auto& part = parts_[index];
            const bool flare = index + 1 == nodes_.size();
            const float age = config.arrivalSeconds - flight.remaining;
            const bool visible = flare ? flight.phase == EFlightPhase::Inbound && age < 6.0f
                                       : flight.phase != EFlightPhase::Idle;
            nodes_[index]->GetComponent<Runtime::RenderComponent>()->SetVisible(visible);
            const glm::vec3 at = flare ? flight.pad + glm::vec3(0, 2.0f + age * 9.0f, 0)
                                       : position + part.position;
            nodes_[index]->SetTransform(at,
                part.rotor ? glm::angleAxis(rotorAngle_, glm::vec3(0,1,0)) : glm::quat(1,0,0,0), part.scale);
        }
        scene.MarkDirty();
    }
}
