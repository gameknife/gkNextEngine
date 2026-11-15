#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Brotato3DAudio.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Components/LightComponent.hpp"

using namespace Brotato3DUtil;

namespace
{
    // The survivors' bus (brotato_vehicles.scad): ~11.4 x 2.6 m with its plough, door on its
    // right toward the front. It drives in along one axis and pulls up level with the player,
    // a lane to the side, so the door is a short dash away rather than right underfoot.
    constexpr float BusHalfLength = 5.75f;
    constexpr float BusHalfWidth = 1.35f;
    constexpr float BusDoorForward = 3.25f;     // door centre ahead of the bus centre
    constexpr float BusDoorStandOff = 1.25f;    // extraction zone centre outside the door
    constexpr float BusStopAhead = 10.0f;       // bus centre this far from the player along its axis
    constexpr float BusLaneOffset = 3.6f;       // and this far to the side
    constexpr float BusApproachDistance = 34.0f;
    constexpr float BusArriveMs = 2300.0f;
    constexpr float BusLightRadius = 7.0f;
    constexpr float BusLightHeight = 4.2f;
    constexpr float SegmentEpsilon = 0.0001f;
    constexpr float NightSkyIntensity = 7.0f;   // dark enough to read as night, light enough to fight in
    constexpr float DaySkyIntensity = 30.0f;
    constexpr float DaySunIntensity = 360.0f;
    constexpr float SunRotationPerSecond = 0.0035f;
    constexpr float SunLightingUpdateIntervalMs = 100.0f;

    bool SegmentIntersectsAabb2D(const glm::vec2& from,
                                 const glm::vec2& to,
                                 const glm::vec2& minBounds,
                                 const glm::vec2& maxBounds)
    {
        const glm::vec2 delta = to - from;
        float tMin = 0.0f;
        float tMax = 1.0f;

        auto clipAxis = [&](float start, float dir, float minValue, float maxValue) -> bool
        {
            if (std::abs(dir) < SegmentEpsilon)
            {
                return start >= minValue && start <= maxValue;
            }

            float enter = (minValue - start) / dir;
            float exit = (maxValue - start) / dir;
            if (enter > exit)
            {
                std::swap(enter, exit);
            }
            tMin = std::max(tMin, enter);
            tMax = std::min(tMax, exit);
            return tMin <= tMax;
        };

        return clipAxis(from.x, delta.x, minBounds.x, maxBounds.x) &&
               clipAxis(from.y, delta.y, minBounds.y, maxBounds.y);
    }

    glm::vec3 ResolveAabbCollisionXZ(const glm::vec3& pos,
                                     float radius,
                                     const glm::vec3& center,
                                     const glm::vec2& halfExtentXZ)
    {
        const float paddedHalfX = halfExtentXZ.x + std::max(0.0f, radius);
        const float paddedHalfZ = halfExtentXZ.y + std::max(0.0f, radius);
        const glm::vec2 delta(pos.x - center.x, pos.z - center.z);
        if (std::abs(delta.x) > paddedHalfX || std::abs(delta.y) > paddedHalfZ)
        {
            return pos;
        }

        glm::vec3 resolved = pos;
        const float overlapX = paddedHalfX - std::abs(delta.x);
        const float overlapZ = paddedHalfZ - std::abs(delta.y);
        if (overlapX < overlapZ)
        {
            const float sign = delta.x < 0.0f ? -1.0f : 1.0f;
            resolved.x = center.x + sign * paddedHalfX;
        }
        else
        {
            const float sign = delta.y < 0.0f ? -1.0f : 1.0f;
            resolved.z = center.z + sign * paddedHalfZ;
        }
        return resolved;
    }

}

void Brotato3DGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                               std::vector<Assets::Model>& models,
                                               std::vector<Assets::FMaterial>& materials,
                                               std::vector<Assets::LightObject>& lights,
                                               std::vector<Assets::AnimationTrack>& tracks)
{
    (void)tracks;
    (void)lights;
    sceneReady_ = false;
    ApplyLightingSettings();
    lightMaterialIds_.clear();
    tempLightPool_.clear();
    playerLightComponent_.reset();
    playerLightNode_.reset();

    auto addLightMaterial = [&materials, this](const glm::vec3& color, float intensity) -> uint32_t
    {
        const std::string key = fmt::format("{}_{:.2f}", LightMaterialKey(color), intensity);
        if (const auto it = lightMaterialIds_.find(key); it != lightMaterialIds_.end())
        {
            return it->second;
        }
        const uint32_t materialId = Assets::SceneBuilder::AddDiffuseLightMaterial(materials, color, intensity);
        lightMaterialIds_[key] = materialId;
        return materialId;
    };
    auto addAreaLight = [&models, &nodes](const std::string& name,
                                                   const glm::vec3& center,
                                                   float radius,
                                                   uint32_t materialId,
                                                   bool visible)
    {
        const glm::vec3 right(radius, 0.0f, 0.0f);
        const glm::vec3 up(0.0f, 0.0f, radius);
        std::vector<Assets::LightObject> createdLights;
        models.push_back(Assets::FProcModel::CreateAreaLight(name,
                                                             -right * 0.5f - up * 0.5f,
                                                             right,
                                                             up,
                                                             materialId,
                                                             createdLights));
        if (createdLights.empty())
        {
            return std::pair<std::shared_ptr<Runtime::LightComponent>, std::shared_ptr<Assets::Node>>{};
        }

        auto node = Assets::SceneBuilder::CreateRenderNode(name,
                                     center,
                                     glm::vec3(1.0f),
                                     static_cast<uint32_t>(nodes.size()),
                                     static_cast<uint32_t>(models.size() - 1),
                                     materialId,
                                     visible);
        // The emitter is a gameplay light proxy, not world geometry. Keep the glowing
        // panel visible and ray-traceable, but do not let it darken characters via sun shadows.
        if (auto render = node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetCastShadows(false);
        }
        auto lightComponent = std::make_shared<Runtime::LightComponent>(createdLights.front());
        node->AddComponent(lightComponent);
        nodes.push_back(node);
        return std::pair{lightComponent, node};
    };

    const auto playerLight = addAreaLight("Brotato3D_PlayerEmissiveLight",
                                          player_.worldPos + glm::vec3(0.0f, 3.2f, 0.0f),
                                          6.0f,
                                          addLightMaterial(glm::vec3(1.0f, 0.86f, 0.62f), 180.0f),
                                          true);
    playerLightComponent_ = playerLight.first;
    playerLightNode_ = playerLight.second;
    // The bus brings its own light: at dusk the door has to be the brightest spot on screen.
    const auto busLight = addAreaLight("Brotato3D_BusFloodLight", HiddenPosition, BusLightRadius,
                                       addLightMaterial(glm::vec3(1.0f, 0.80f, 0.52f), 240.0f), false);
    busLightComponent_ = busLight.first;
    busLightNode_ = busLight.second;
    tempLightPool_.reserve(32);
    for (int index = 0; index < 32; ++index)
    {
        const auto tempLight = addAreaLight(fmt::format("Brotato3D_TempEmissiveLight_{}", index),
                                            HiddenPosition,
                                            1.0f,
                                            addLightMaterial(glm::vec3(1.0f), 0.0f),
                                            false);
        tempLightPool_.push_back({.lightComponent = tempLight.first, .node = tempLight.second});
    }

    characterMaterialIds_.clear();
    for (const Brotato3D::FCharacterDef& character : characterDefs_)
    {
        characterMaterialIds_[character.id] = Assets::SceneBuilder::AddLambertianMaterial(materials, character.color);
    }
    const uint32_t playerMaterialId =
        characterMaterialIds_.contains(selectedCharacterId_) ? characterMaterialIds_[selectedCharacterId_] :
                                                               Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.20f, 0.75f, 0.30f));
    playerRig_.InjectAssets(models, materials, playerMaterialId);

    // Collision, weapons, and gameplay queries retain a logical player root;
    // the old visible sphere is replaced by the ScadRig instantiated after load.
    player_.bodyNode = Assets::Node::CreateNode(
        "Brotato3D_Player", player_.worldPos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f), static_cast<uint32_t>(nodes.size()));
    nodes.push_back(player_.bodyNode);

    player_.facingNode = Assets::Node::CreateNode(
        "Brotato3D_PlayerFacing", glm::vec3(0.0f, 0.45f, -0.25f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f),
        static_cast<uint32_t>(nodes.size()));
    player_.facingNode->SetParent(player_.bodyNode);
    nodes.push_back(player_.facingNode);

    extractionVehicleNodes_.clear();
    extractionVehicleRootNode_.reset();
    extractionBeaconNode_.reset();
    extractionVehicleMaterialId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.55f, 0.36f, 0.08f));
    extractionVehicleActiveMaterialId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.30f, 1.0f, 0.45f));
    extractionVehicleRootNode_ = Assets::Node::CreateNode("Brotato3D_ExtractionBus", HiddenPosition,
                                                          glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f),
                                                          static_cast<uint32_t>(nodes.size()));
    nodes.push_back(extractionVehicleRootNode_);
    auto addBusNode = [&nodes, this](const std::string& name, uint32_t modelId,
                                     const std::array<uint32_t, 16>& sectionMaterials, const glm::vec3& localPos)
    {
        auto node = Assets::SceneBuilder::CreateRenderNode(name, localPos, glm::vec3(1.0f),
                                                           static_cast<uint32_t>(nodes.size()), modelId,
                                                           sectionMaterials[0], false);
        if (auto render = node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetMaterials(sectionMaterials);
        }
        node->SetParent(extractionVehicleRootNode_);
        nodes.push_back(node);
        extractionVehicleNodes_.push_back(node);
        return node;
    };
    bool busBuilt = false;
    if (vehicleCatalogLoaded_)
    {
        const int32_t busBone = vehicleCatalog_.FindBone("bone_veh_bus");
        for (const Assets::FRigPart& part : vehicleCatalog_.parts)
        {
            if (part.bone != busBone || part.modelIndex < 0)
            {
                continue;
            }
            models.push_back(vehicleCatalog_.partModels[part.modelIndex]);
            std::array<uint32_t, 16> sectionMaterials{};
            for (size_t section = 0; section < part.sectionColors.size() && section < sectionMaterials.size(); ++section)
            {
                sectionMaterials[section] = Assets::SceneBuilder::AddRigSectionMaterial(materials, part, section);
            }
            addBusNode(fmt::format("Brotato3D_ExtractionBus_Part{}", extractionVehicleNodes_.size()),
                       static_cast<uint32_t>(models.size() - 1), sectionMaterials, glm::vec3(0.0f));
            busBuilt = true;
        }
    }
    if (!busBuilt)
    {
        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-BusHalfWidth, 0.0f, -BusHalfLength),
                                                       glm::vec3(BusHalfWidth, 2.8f, BusHalfLength)));
        std::array<uint32_t, 16> bodyMaterials{};
        bodyMaterials.fill(extractionVehicleMaterialId_);
        addBusNode("Brotato3D_ExtractionBus_Fallback", static_cast<uint32_t>(models.size() - 1), bodyMaterials, glm::vec3(0.0f));
    }
    // The beacon on the roof turns green while the player is boarding.
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.22f, 0.0f, -0.22f), glm::vec3(0.22f, 0.3f, 0.22f)));
    std::array<uint32_t, 16> beaconMaterials{};
    beaconMaterials.fill(extractionVehicleMaterialId_);
    extractionBeaconNode_ = addBusNode("Brotato3D_ExtractionBus_Beacon", static_cast<uint32_t>(models.size() - 1),
                                       beaconMaterials, glm::vec3(0.0f, 3.0f, -1.2f));

    enemyVisuals_.clear();
    for (const auto& [enemyId, def] : enemyDefs_)
    {
        models.push_back(Assets::FProcModel::CreateBox(-def.size * 0.5f, def.size * 0.5f));
        FEnemyVisualResource visual{};
        visual.modelId = static_cast<uint32_t>(models.size() - 1);
        // The box is only the hidden proxy of the kinematic push body; the zombie itself is a
        // ScadRig. These two are what flies off it when hit: flesh in the type's colour, and gore.
        const glm::vec3 flesh = glm::mix(def.color, glm::vec3(0.62f, 0.06f, 0.05f), 0.55f);
        visual.materialId = Assets::SceneBuilder::AddLambertianMaterial(materials, flesh);
        visual.darkMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, flesh * 0.45f);
        visual.hitFlashMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f));
        visual.warningMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f, 0.08f, 0.06f));
        visual.phase2MaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.95f, 0.20f, 0.10f));
        visual.baseColor = def.color;
        enemyVisuals_[enemyId] = visual;
        if (const auto rigIt = zombieRigs_.find(enemyId); rigIt != zombieRigs_.end() && rigIt->second)
        {
            rigIt->second->Inject(models, materials, def.tints);
        }
    }

    projectilePool_.clear();
    projectilePool_.reserve(weaponDefs_.size() * 128);
    for (const auto& [weaponId, weaponDef] : weaponDefs_)
    {
        models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), weaponDef.projectileSize));
        const uint32_t weaponProjectileModelId = static_cast<uint32_t>(models.size() - 1);
        const uint32_t weaponProjectileMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, weaponDef.projectileColor);
        if (weaponId == "smg")
        {
            projectileModelId_ = weaponProjectileModelId;
            projectileMaterialId_ = weaponProjectileMaterialId;
        }

        for (int index = 0; index < 128; ++index)
        {
            auto node = Assets::SceneBuilder::CreateRenderNode(fmt::format("Brotato3D_Projectile_{}_{}", weaponId, index), HiddenPosition,
                                         glm::vec3(1.0f), static_cast<uint32_t>(nodes.size()), weaponProjectileModelId,
                                         weaponProjectileMaterialId, false);
            nodes.push_back(node);
            Brotato3D::FProjectileRuntime projectile{};
            projectile.weaponId = weaponId;
            projectile.modelId = weaponProjectileModelId;
            projectile.materialId = weaponProjectileMaterialId;
            projectile.radius = weaponDef.projectileSize;
            projectile.node = node;
            projectilePool_.push_back(projectile);
        }
    }

    enemyProjectileMaterialIds_.clear();
    for (const auto& [enemyId, enemyDef] : enemyDefs_)
    {
        (void)enemyId;
        if (enemyDef.ranged.enabled)
        {
            enemyProjectileMaterialIds_[&enemyDef] = Assets::SceneBuilder::AddLambertianMaterial(materials, enemyDef.ranged.color);
        }
    }
    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 1.0f));
    enemyProjectileModelId_ = static_cast<uint32_t>(models.size() - 1);
    enemyProjectileMaterialId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.3f, 0.95f, 0.2f));
    enemyProjectilePool_.clear();
    enemyProjectilePool_.reserve(128);
    for (int index = 0; index < 128; ++index)
    {
        auto node = Assets::SceneBuilder::CreateRenderNode(fmt::format("Brotato3D_EnemyProjectile_{}", index), HiddenPosition, glm::vec3(1.0f),
                                     static_cast<uint32_t>(nodes.size()), enemyProjectileModelId_,
                                     enemyProjectileMaterialId_, false);
        Assets::NodeUtils::SetOutlineFlags(node, Runtime::RenderOutlineFlags::danger);
        nodes.push_back(node);
        Brotato3D::FEnemyProjectileRuntime projectile{};
        projectile.radius = 0.18f;
        projectile.node = node;
        enemyProjectilePool_.push_back(projectile);
    }

    BuildDebrisPool(models, materials, nodes);
    BuildGorePool(models, materials, nodes);
    BuildArenaWallBodies();
    BuildKinematicCollisionBodies(models, materials, nodes);

    sceneReady_ = true;
    if (appState_ == Brotato3D::EAppState::MainMenu || appState_ == Brotato3D::EAppState::CharacterSelect)
    {
        Assets::NodeUtils::SetVisible(player_.bodyNode, false);
        playerRig_.SetVisible(false);
    }
}

bool Brotato3DGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    glm::vec3 cameraTarget = cameraSmoothedTarget_;
    // A slightly flatter angle keeps the arena readable while exposing the
    // ScadRig hero's upper/lower-body pose instead of collapsing it top-down.
    glm::vec3 cameraPosition = cameraTarget + glm::vec3(0.0f, 13.0f, 10.0f);
    if (validation_.cameraHeight > 0.0f)
    {
        cameraTarget.y = 0.9f;
        cameraPosition = cameraTarget + glm::vec3(0.0f, validation_.cameraHeight, validation_.cameraBack);
    }
    if (Brotato3D::ScreenShakeEnabled && screenShakeMs_ > 0.0f)
    {
        const float strength = std::min(0.25f, screenShakeIntensity_ * 0.05f);
        const float t = runElapsedSec_ * 37.0f;
        const glm::vec3 jitter(std::sin(t), 0.0f, std::cos(t * 1.37f));
        cameraPosition += jitter * strength;
        cameraTarget += jitter * (strength * 0.5f);
    }
    outRenderCamera.ModelView = glm::lookAtRH(cameraPosition, cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    outRenderCamera.FieldOfView = 60.0f;
    return true;
}

void Brotato3DGameInstance::OnSceneLoaded()
{
    ApplyLightingSettings();
    playerRig_.OnSceneLoaded(GetEngine().GetScene());
    playerRig_.SetVisible(appState_ != Brotato3D::EAppState::MainMenu &&
                          appState_ != Brotato3D::EAppState::CharacterSelect);
    sceneReady_ = true;
}

void Brotato3DGameInstance::OnSceneUnloaded()
{
    sceneReady_ = false;
    playerRig_.OnSceneUnloaded();
    // Zombie bodies are scene nodes: they do not survive a scene swap.
    enemies_.clear();
    corpseCount_ = 0;
}

void Brotato3DGameInstance::SetExtractionVehicleVisible(bool visible)
{
    extractionVehicleVisible_ = visible;
    for (const auto& node : extractionVehicleNodes_)
    {
        if (node)
        {
            Assets::NodeUtils::SetVisible(node, visible);
        }
    }
    if (!visible && extractionVehicleRootNode_)
    {
        extractionVehicleRootNode_->SetTranslation(HiddenPosition);
    }
    if (busLightNode_)
    {
        Assets::NodeUtils::SetVisible(busLightNode_, visible);
        if (!visible)
        {
            UpdateLightArea(busLightComponent_.get(), HiddenPosition, 0.01f, 0.0f);
            busLightNode_->SetTranslation(HiddenPosition);
        }
    }
}

void Brotato3DGameInstance::ResetExtractionVehicle()
{
    extractionVehiclePos_ = HiddenPosition;
    extractionVehicleStartPos_ = HiddenPosition;
    extractionVehicleTargetPos_ = HiddenPosition;
    extractionVehicleAnimMs_ = 0.0f;
    extractionVehicleAnimTotalMs_ = 0.0f;
    SetExtractionVehicleVisible(false);
}

glm::vec3 Brotato3DGameInstance::GetExtractionZonePos() const
{
    const glm::vec3 right = glm::cross(extractionVehicleForward_, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::vec3 zone = extractionVehiclePos_ + extractionVehicleForward_ * BusDoorForward +
                     right * (BusHalfWidth + BusDoorStandOff);
    zone.y = 0.05f;
    return zone;
}

void Brotato3DGameInstance::BeginDuskSurge()
{
    const int side = std::uniform_int_distribution<int>(0, 3)(rng_);
    static constexpr std::array<glm::vec3, 4> OutwardDirections = {
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
    };
    // The camera looks north from the south, so only ~8 m of ground shows below the player:
    // the bus comes from the north, east or west, and from the south only when it has to.
    const auto availableDistance = [this](const glm::vec3& dir)
    {
        return dir.x > 0.0f ? arenaHalfExtent_.x - player_.worldPos.x :
               dir.x < 0.0f ? arenaHalfExtent_.x + player_.worldPos.x :
               dir.z > 0.0f ? arenaHalfExtent_.y - player_.worldPos.z :
                              arenaHalfExtent_.y + player_.worldPos.z;
    };
    glm::vec3 outward = OutwardDirections[1];
    bool placed = false;
    for (int attempt = 0; attempt < 3 && !placed; ++attempt)
    {
        // OutwardDirections: [0] north (-Z), [1] south (+Z), [2] west, [3] east.
        static constexpr std::array<int, 3> onScreen = {0, 2, 3};
        const glm::vec3 candidate = OutwardDirections[onScreen[(side + attempt) % 3]];
        if (availableDistance(candidate) >= BusStopAhead + BusHalfLength + 2.0f)
        {
            outward = candidate;
            placed = true;
        }
    }

    // The bus drives toward the player (forward = -outward) and stops a lane to the side with
    // its right flank - and the door - facing them.
    extractionVehicleForward_ = -outward;
    const glm::vec3 right = glm::cross(extractionVehicleForward_, glm::vec3(0.0f, 1.0f, 0.0f));
    extractionVehicleHalfExtent_ = std::abs(outward.x) > 0.5f ? glm::vec2(BusHalfLength, BusHalfWidth) :
                                                                 glm::vec2(BusHalfWidth, BusHalfLength);
    extractionVehicleTargetPos_ = ClampToArena(
        player_.worldPos + outward * BusStopAhead - right * BusLaneOffset, BusHalfLength, arenaHalfExtent_);
    extractionVehicleStartPos_ = ClampToArena(
        extractionVehicleTargetPos_ + outward * BusApproachDistance, BusHalfLength, arenaHalfExtent_);
    extractionVehicleStartPos_.y = 0.0f;
    extractionVehicleTargetPos_.y = 0.0f;
    extractionVehiclePos_ = extractionVehicleStartPos_;
    extractionVehicleAnimTotalMs_ = BusArriveMs;
    extractionVehicleAnimMs_ = extractionVehicleAnimTotalMs_;
    if (extractionVehicleRootNode_)
    {
        extractionVehicleRootNode_->SetTransform(
            extractionVehiclePos_,
            glm::angleAxis(std::atan2(extractionVehicleForward_.x, extractionVehicleForward_.z), glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::vec3(1.0f));
    }
    SetExtractionVehicleVisible(true);
    SetSkyIntensityTarget(NightSkyIntensity, 1500.0f);
    waveBannerText_ = "NIGHTFALL";
    waveBannerMs_ = 1500.0f;
    StartScreenShake(260.0f, 2.2f);
    Brotato3D::StartBgm("battle");
}

void Brotato3DGameInstance::UpdateExtractionVehicle(double deltaSeconds)
{
    if (waveSystem_.GetState() != Brotato3D::EWaveState::DuskSurge)
    {
        waveSystem_.NotifyPlayerInExtractionZone(false);
        return;
    }

    if (extractionVehicleAnimMs_ > 0.0f)
    {
        extractionVehicleAnimMs_ = std::max(0.0f, extractionVehicleAnimMs_ - static_cast<float>(deltaSeconds * 1000.0));
        const float t = 1.0f - extractionVehicleAnimMs_ / std::max(1.0f, extractionVehicleAnimTotalMs_);
        // Arrives fast and brakes hard: ease-out, not a symmetric smoothstep.
        const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        extractionVehiclePos_ = glm::mix(extractionVehicleStartPos_, extractionVehicleTargetPos_, eased);
        if (extractionVehicleRootNode_)
        {
            extractionVehicleRootNode_->SetTranslation(extractionVehiclePos_);
        }
        // Roadkill: whatever stands in the lane while the bus is still rolling comes apart.
        int flattened = 0;
        const glm::vec3 right = glm::cross(extractionVehicleForward_, glm::vec3(0.0f, 1.0f, 0.0f));
        for (Brotato3D::FEnemyRuntime& enemy : enemies_)
        {
            if (!enemy.alive || !enemy.def || enemy.def->boss.enabled)
            {
                continue;
            }
            const glm::vec2 offset(enemy.worldPos.x - extractionVehiclePos_.x, enemy.worldPos.z - extractionVehiclePos_.z);
            if (std::abs(offset.x) > extractionVehicleHalfExtent_.x + enemy.radius ||
                std::abs(offset.y) > extractionVehicleHalfExtent_.y + enemy.radius)
            {
                continue;
            }
            const float side = glm::dot(glm::vec3(offset.x, 0.0f, offset.y), right) >= 0.0f ? 1.0f : -1.0f;
            enemy.lastHitDebrisDir = glm::normalize(extractionVehicleForward_ * 1.6f + right * side);
            enemy.lastHitExplosive = true;
            enemy.lastHitDamage = std::max(1, enemy.currentHp);
            enemy.currentHp = 0;
            KillEnemy(enemy, true);
            ++flattened;
        }
        if (flattened > 0)
        {
            StartScreenShake(120.0f, std::min(3.5f, 1.2f + 0.4f * static_cast<float>(flattened)));
        }
    }
    else
    {
        extractionVehiclePos_ = extractionVehicleTargetPos_;
    }

    const Brotato3D::FWaveDef* waveDef = waveSystem_.GetCurrentWaveDef();
    const float radius = waveDef ? waveDef->extractionRadiusM : 2.5f;
    const bool arrived = extractionVehicleAnimMs_ <= 0.0f;
    const bool inZone = extractionVehicleVisible_ && arrived &&
                        DistanceXZ(player_.worldPos, GetExtractionZonePos()) <= radius;
    waveSystem_.NotifyPlayerInExtractionZone(inZone);
    const uint32_t materialId = inZone ? extractionVehicleActiveMaterialId_ : extractionVehicleMaterialId_;
    if (extractionBeaconNode_ && materialId != 0)
    {
        Assets::NodeUtils::SetPrimaryMaterial(extractionBeaconNode_, materialId);
    }
    if (busLightNode_)
    {
        const glm::vec3 lightPos = GetExtractionZonePos() + glm::vec3(0.0f, BusLightHeight, 0.0f);
        UpdateLightArea(busLightComponent_.get(), lightPos, BusLightRadius, 1.0f);
        busLightNode_->SetTranslation(lightPos);
    }
}

bool Brotato3DGameInstance::IsExtractionVehicleObstacleActive() const
{
    return extractionVehicleVisible_;
}

glm::vec3 Brotato3DGameInstance::ResolveExtractionVehicleCollision(const glm::vec3& pos, float radius) const
{
    glm::vec3 resolved = pos;
    if (IsExtractionVehicleObstacleActive())
    {
        resolved = ResolveAabbCollisionXZ(resolved,
                                          radius,
                                          extractionVehiclePos_,
                                          extractionVehicleHalfExtent_);
    }

    return resolved;
}

glm::vec3 Brotato3DGameInstance::ResolvePlayerObstacleCollision(const glm::vec3& pos, float radius)
{
    glm::vec3 resolved = pos;
    if (IsExtractionVehicleObstacleActive())
    {
        resolved = ResolveAabbCollisionXZ(resolved,
                                          radius,
                                          extractionVehiclePos_,
                                          extractionVehicleHalfExtent_);
    }

    return resolved;
}

bool Brotato3DGameInstance::IsSegmentBlockedByExtractionVehicle(const glm::vec3& from,
                                                                const glm::vec3& to,
                                                                float radius) const
{
    const float padding = std::max(0.0f, radius);
    const glm::vec2 fromXZ(from.x, from.z);
    const glm::vec2 toXZ(to.x, to.z);
    if (IsExtractionVehicleObstacleActive())
    {
        const glm::vec2 minBounds(extractionVehiclePos_.x - extractionVehicleHalfExtent_.x - padding,
                                  extractionVehiclePos_.z - extractionVehicleHalfExtent_.y - padding);
        const glm::vec2 maxBounds(extractionVehiclePos_.x + extractionVehicleHalfExtent_.x + padding,
                                  extractionVehiclePos_.z + extractionVehicleHalfExtent_.y + padding);
        if (SegmentIntersectsAabb2D(fromXZ, toXZ, minBounds, maxBounds))
        {
            return true;
        }
    }

    return false;
}

glm::vec3 Brotato3DGameInstance::ResolveEnemyGroundedPosition(const Brotato3D::FEnemyRuntime& enemy, glm::vec3 candidate) const
{
    candidate = ClampToArena(candidate, enemy.radius, arenaHalfExtent_);
    candidate = ResolveExtractionVehicleCollision(candidate, enemy.radius);
    candidate = ClampToArena(candidate, enemy.radius, arenaHalfExtent_);
    if (enemy.def)
    {
        candidate.y = enemy.def->size.y * 0.5f;
    }
    return candidate;
}

void Brotato3DGameInstance::UpdateCombatEffects(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    UpdateGroundIndicators(deltaSeconds);
    for (auto& ring : explosionRings_)
    {
        ring.remainingMs -= deltaMs;
    }
    explosionRings_.erase(std::remove_if(explosionRings_.begin(), explosionRings_.end(),
                                         [](const Brotato3D::FExpandingRing& ring)
                                         {
                                             return ring.remainingMs <= 0.0f;
                                         }),
                          explosionRings_.end());

    for (auto& beam : laserBeams_)
    {
        beam.remainingMs -= deltaMs;
    }
    laserBeams_.erase(std::remove_if(laserBeams_.begin(), laserBeams_.end(),
                                     [](const Brotato3D::FLaserBeam& beam)
                                     {
                                         return beam.remainingMs <= 0.0f;
                                     }),
                      laserBeams_.end());

    for (auto& flash : muzzleFlashes_)
    {
        flash.remainingMs -= deltaMs;
    }
    muzzleFlashes_.erase(std::remove_if(muzzleFlashes_.begin(), muzzleFlashes_.end(),
                                        [](const Brotato3D::FMuzzleFlash& flash)
                                        {
                                            return flash.remainingMs <= 0.0f;
                                        }),
                         muzzleFlashes_.end());

    const glm::vec3 playerLightPos = player_.worldPos + glm::vec3(0.0f, 3.2f, 0.0f);
    UpdateLightArea(playerLightComponent_.get(), playerLightPos, 6.0f, 1.0f);
    playerLightNode_->SetTranslation(playerLightPos);
    for (auto& light : tempLightPool_)
    {
        if (!light.active)
        {
            continue;
        }

        light.remainingMs -= deltaMs;
        const float intensityScale = light.durationMs > 0.0f ? std::clamp(light.remainingMs / light.durationMs, 0.0f, 1.0f) : 0.0f;
        if (light.remainingMs <= 0.0f)
        {
            light.active = false;
            light.remainingMs = 0.0f;
            UpdateLightArea(light.lightComponent.get(), HiddenPosition, 0.01f, 0.0f);
            light.node->SetTranslation(HiddenPosition);
            Assets::NodeUtils::SetVisible(light.node, false);
            continue;
        }
        UpdateLightArea(light.lightComponent.get(), light.worldPos, light.radiusMeters, intensityScale);
        light.node->SetTranslation(light.worldPos);
        light.node->SetScale(glm::vec3(1.0f));
    }
}

void Brotato3DGameInstance::UpdateGroundIndicators(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    for (auto& indicator : groundIndicators_)
    {
        indicator.remainingMs -= deltaMs;
    }
    groundIndicators_.erase(std::remove_if(groundIndicators_.begin(), groundIndicators_.end(),
                                           [](const Brotato3D::FGroundIndicator& indicator)
                                           {
                                               return indicator.remainingMs <= 0.0f;
                                           }),
                            groundIndicators_.end());
}

void Brotato3DGameInstance::UpdateWaveBanner(double deltaSeconds)
{
    if (waveBannerMs_ > 0.0f)
    {
        waveBannerMs_ = std::max(0.0f, waveBannerMs_ - static_cast<float>(deltaSeconds * 1000.0));
    }
}

void Brotato3DGameInstance::UpdateFloatingTexts(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    for (auto& text : floatingTexts_)
    {
        text.remainingMs -= deltaMs;
    }
    floatingTexts_.erase(std::remove_if(floatingTexts_.begin(), floatingTexts_.end(),
                                        [](const Brotato3D::FFloatingText& text)
                                        {
                                            return text.remainingMs <= 0.0f;
                                        }),
                         floatingTexts_.end());
}

void Brotato3DGameInstance::PushExplosionRing(const glm::vec3& worldPos, const glm::vec4& color, float maxRadius)
{
    explosionRings_.push_back({worldPos, color, 300.0f, 300.0f, maxRadius});
}

void Brotato3DGameInstance::PushLaserBeam(const glm::vec3& from,
                                          const glm::vec3& to,
                                          const glm::vec4& color,
                                          float durationMs,
                                          float width)
{
    laserBeams_.push_back({from, to, color, durationMs, durationMs, width});
}

void Brotato3DGameInstance::PushGroundIndicator(const Brotato3D::FGroundIndicator& indicator)
{
    if (indicator.enemyTag != 0)
    {
        CancelGroundIndicatorsForEnemy(indicator.enemyTag);
    }
    if (groundIndicators_.size() >= 64)
    {
        groundIndicators_.erase(groundIndicators_.begin());
    }
    groundIndicators_.push_back(indicator);
}

void Brotato3DGameInstance::CancelGroundIndicatorsForEnemy(uint32_t enemyTag)
{
    if (enemyTag == 0)
    {
        return;
    }
    groundIndicators_.erase(std::remove_if(groundIndicators_.begin(), groundIndicators_.end(),
                                           [enemyTag](const Brotato3D::FGroundIndicator& indicator)
                                           {
                                               return indicator.enemyTag == enemyTag;
                                           }),
                            groundIndicators_.end());
}

void Brotato3DGameInstance::PushMuzzleFlash(const glm::vec3& worldPos, const glm::vec3& color)
{
    muzzleFlashes_.push_back({worldPos, color, 80.0f, 80.0f});
}

void Brotato3DGameInstance::SpawnTempLight(const glm::vec3& worldPos,
                                           const glm::vec3& color,
                                           float radiusMeters,
                                           float durationMs)
{
    if (tempLightPool_.empty())
    {
        return;
    }

    auto lightIt = std::find_if(tempLightPool_.begin(), tempLightPool_.end(),
                                [](const FTempLightRuntime& light)
                                {
                                    return !light.active;
                                });
    if (lightIt == tempLightPool_.end())
    {
        lightIt = std::min_element(tempLightPool_.begin(), tempLightPool_.end(),
                                   [](const FTempLightRuntime& lhs, const FTempLightRuntime& rhs)
                                   {
                                       return lhs.remainingMs < rhs.remainingMs;
                                   });
    }
    if (lightIt == tempLightPool_.end())
    {
        return;
    }

    lightIt->active = true;
    lightIt->worldPos = worldPos;
    lightIt->radiusMeters = radiusMeters;
    lightIt->durationMs = durationMs;
    lightIt->remainingMs = durationMs;

    if (lightIt->lightComponent)
    {
        lightIt->lightComponent->SetMaterialIndex(EnsureLightMaterial(color));
    }
    Assets::NodeUtils::SetPrimaryMaterial(lightIt->node, EnsureLightMaterial(color));
    lightIt->node->SetTranslation(worldPos);
    lightIt->node->SetScale(glm::vec3(1.0f));
    Assets::NodeUtils::SetVisible(lightIt->node, true);
    UpdateLightArea(lightIt->lightComponent.get(), worldPos, radiusMeters, 1.0f);
}

void Brotato3DGameInstance::UpdateLightArea(Runtime::LightComponent* lightComponent,
                                            const glm::vec3& worldPos,
                                            float radiusMeters,
                                            float intensityScale)
{
    if (!lightComponent || lightComponent->Lights().empty())
    {
        return;
    }

    const float radius = std::max(0.01f, radiusMeters);
    const glm::vec3 right(radius, 0.0f, 0.0f);
    const glm::vec3 up(0.0f, 0.0f, radius);
    auto& light = lightComponent->Lights().front();
    (void)worldPos;
    light.p0 = glm::vec4(-right * 0.5f - up * 0.5f, 1.0f);
    light.p1 = glm::vec4(-right * 0.5f + up * 0.5f, 1.0f);
    light.p3 = glm::vec4(right * 0.5f - up * 0.5f, 1.0f);
    light.normal_area = glm::vec4(0.0f, -1.0f, 0.0f, radius * radius * std::clamp(intensityScale, 0.0f, 1.0f));
}

uint32_t Brotato3DGameInstance::EnsureLightMaterial(const glm::vec3& color)
{
    const std::string key = LightMaterialKey(color);
    if (const auto it = lightMaterialIds_.find(key); it != lightMaterialIds_.end())
    {
        return it->second;
    }

    const uint32_t materialId = Assets::SceneBuilder::AddDiffuseLightMaterialToScene(GetEngine().GetScene(), color, 650.0f);
    lightMaterialIds_[key] = materialId;
    return materialId;
}

void Brotato3DGameInstance::StartScreenShake(float durationMs, float intensity)
{
    if (!Brotato3D::ScreenShakeEnabled)
    {
        return;
    }
    screenShakeMs_ = std::max(screenShakeMs_, durationMs);
    screenShakeIntensity_ = std::max(screenShakeIntensity_, intensity);
}

void Brotato3DGameInstance::BeginWaveBanner()
{
    if (waveSystem_.GetState() != Brotato3D::EWaveState::Active)
    {
        return;
    }

    const int waveIndex = waveSystem_.GetCurrentWaveIndex();
    const int waveCount = waveSystem_.GetWaveCount();
    const Brotato3D::FWaveDef* waveDef = waveSystem_.GetCurrentWaveDef();
    const bool bossWave = waveDef && waveDef->bgmCue == "boss";
    Brotato3D::PlayWaveStartSfx(waveIndex);
    Brotato3D::StartBgm(waveDef ? waveDef->bgmCue : std::string("calm"));
    waveBannerText_ = bossWave ? "BOSS INCOMING" : fmt::format("WAVE {} / {}", waveIndex + 1, waveCount);
    waveBannerMs_ = 1000.0f;
    if (bossWave)
    {
        StartScreenShake(250.0f, 2.0f);
    }
}

void Brotato3DGameInstance::ApplyLightingSettings()
{
    auto& envSettings = GetEngine().GetScene().GetEnvSettings();
    const float daylight = std::clamp(
        (currentSkyIntensity_ - NightSkyIntensity) / (DaySkyIntensity - NightSkyIntensity),
        0.0f,
        1.0f);
    const float sunArc = 0.5f + 0.5f * std::sin(sunRotation_ * glm::pi<float>());

    envSettings.HasSky = true;
    envSettings.HasSun = daylight > 0.01f;
    envSettings.SkyIntensity = currentSkyIntensity_;
    envSettings.SunRotation = sunRotation_;
    envSettings.SunElevation = glm::radians(38.0f + 14.0f * sunArc);
    envSettings.SunIntensity = DaySunIntensity * daylight;
    envSettings.SunColor = glm::mix(glm::vec3(1.0f, 0.72f, 0.48f),
                                    glm::vec3(1.0f, 0.94f, 0.82f),
                                    daylight);
}

void Brotato3DGameInstance::SetSkyIntensityTarget(float target, float transitionMs)
{
    skyTransitionStartIntensity_ = currentSkyIntensity_;
    targetSkyIntensity_ = target;
    skyTransitionTotalMs_ = std::max(1.0f, transitionMs);
    skyTransitionRemainingMs_ = skyTransitionTotalMs_;
    if (transitionMs <= 0.0f)
    {
        currentSkyIntensity_ = targetSkyIntensity_;
        skyTransitionRemainingMs_ = 0.0f;
        ApplyLightingSettings();
    }
}

void Brotato3DGameInstance::UpdateSkyTransition(double deltaSeconds)
{
    if (skyTransitionRemainingMs_ <= 0.0f)
    {
        return;
    }

    skyTransitionRemainingMs_ = std::max(0.0f, skyTransitionRemainingMs_ - static_cast<float>(deltaSeconds * 1000.0));
    const float t = 1.0f - skyTransitionRemainingMs_ / std::max(1.0f, skyTransitionTotalMs_);
    currentSkyIntensity_ = glm::mix(skyTransitionStartIntensity_, targetSkyIntensity_, std::clamp(t, 0.0f, 1.0f));
    ApplyLightingSettings();
}

void Brotato3DGameInstance::UpdateSunLighting(double deltaSeconds)
{
    const float daylight = std::clamp(
        (currentSkyIntensity_ - NightSkyIntensity) / (DaySkyIntensity - NightSkyIntensity),
        0.0f,
        1.0f);
    if (daylight <= 0.01f)
    {
        return;
    }

    sunRotation_ = std::fmod(
        sunRotation_ + static_cast<float>(deltaSeconds) * SunRotationPerSecond,
        2.0f);
    sunLightingUpdateAccumMs_ += static_cast<float>(deltaSeconds * 1000.0);
    if (sunLightingUpdateAccumMs_ < SunLightingUpdateIntervalMs)
    {
        return;
    }
    sunLightingUpdateAccumMs_ = std::fmod(sunLightingUpdateAccumMs_, SunLightingUpdateIntervalMs);
    ApplyLightingSettings();
}

bool Brotato3DGameInstance::IsDuskSurgeActive() const
{
    return waveSystem_.GetState() == Brotato3D::EWaveState::DuskSurge;
}

float Brotato3DGameInstance::GetExtractionRadius() const
{
    const Brotato3D::FWaveDef* waveDef = waveSystem_.GetCurrentWaveDef();
    return waveDef ? waveDef->extractionRadiusM : 2.5f;
}


