#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/UI/Localization.hpp"
#include "NextDayzGameInstance.hpp"
#include "Application/Game/NextDayz/World/Economy.hpp"

#include <imgui.h>
#include <SDL3/SDL_keycode.h>
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"
#include "Engine/Runtime/Platform/UserPaths.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Options.hpp"

bool NextDayzGameInstance::CampaignEnabled() const
{
    // A networked survivor plays straight in the map (the Master/Hive owns
    // storage later); offline keeps the safehouse campaign.
    if (trainingSession_ || netMode_ != ENetMode::Offline) return false;
    return !GOption->AgentValidation || validationCampaign_;
}

std::string NextDayzGameInstance::ActiveMapId() const
{
    const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_);
    return map ? map->id : campaignMap_;
}

void NextDayzGameInstance::LoadEconomy()
{
    std::string error;
    if (!NextDayz::LoadEconomyDefs(Utilities::FileHelper::ResolvePlatformFilePath(
            "assets/configs/nextdayz_economy.json"), economy_, error))
    {
        // Without a valid table nothing sells and the trader stays closed.
        economy_ = {};
        SPDLOG_ERROR("[NextDayz] economy table: {}", error);
    }
}

void NextDayzGameInstance::InitializeCampaign()
{
    if (!CampaignEnabled()) return;
    if (cabinScene_) { InitializeCabinCampaign(); return; }
    switchingCampaignContext_ = false;
    LoadEconomy();
    std::string error;
    if (!NextDayz::LoadExtractionConfig(Utilities::FileHelper::ResolvePlatformFilePath(
        "assets/configs/nextdayz_extraction.json"), extractionConfig_, error))
        SPDLOG_ERROR("[NextDayz] extraction config: {}; using defaults", error);
    campaignPath_ = GOption->AgentValidation ? std::filesystem::path(validationCampaignPath_)
        : NextPlatform::UserPaths::EnsureUserFile("NextDayz", "campaign/save.json");
    if (authoringMode_ && !GOption->AgentValidation)
        campaignPath_ = NextPlatform::UserPaths::EnsureUserFile("NextDayz", "authoring/" + ActiveMapId() + ".json");
    campaignSaveBlocked_ = false;
    saveError_.clear();
    std::error_code ec;
    const bool freshFixture = GOption->AgentValidation && std::exchange(validationCampaignFresh_, false);
    const bool exists = !freshFixture && (std::filesystem::exists(campaignPath_, ec) ||
        std::filesystem::exists(campaignPath_.string() + ".bak", ec));
    if (pendingCampaign_)
    {
        auto saved = std::move(*pendingCampaign_);
        pendingCampaign_.reset();
        bool upgraded = false;
        const auto* destination = NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_);
        const bool legacy = saved.map == NextDayz::LegacyRiverlandScene;
        const std::string retired = NextDayz::RetiredExpeditionMapId(expeditionMaps_, saved.map);
        if (saved.map != campaignMap_ && destination && (legacy || !retired.empty()))
        {
            const auto archive = std::filesystem::path(campaignPath_.string() +
                (legacy ? ".riverland-backup" : fmt::format(".retired-{}-backup", retired)));
            if ((!std::filesystem::exists(archive, ec) && !NextDayz::WriteCampaignSave(archive, saved, error)) || ec ||
                !NextDayz::RetireExpeditionCampaign(saved, *destination,
                    {loot_.Capture(), time_.Capture(), destination->version}, player_.Position(),
                    legacy ? std::string(NextDayz::LegacyRiverlandScene) : retired, "RETURNED / REGION RETIRED"))
            {
                campaignSaveBlocked_ = true;
                saveError_ = error.empty() ? "Cannot migrate retired region" : error;
                campaignMessage_ = NextDayz::L10n::FText("Cannot restore expedition: ") + saveError_;
                return;
            }
        }
        if (const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_);
            map && saved.mapVersion < map->version)
        {
            auto target = *map;
            target.scene = campaignMap_;
            upgraded = NextDayz::UpgradeExpeditionCampaign(saved,target,
                {loot_.Capture(),time_.Capture(),target.version},player_.Position());
        }
        if (pendingFreshMap_)
        {
            saved.world = loot_.Capture();
            saved.time = time_.Capture();
            saved.position = player_.Position();
            pendingFreshMap_ = false;
        }
        if (!RestoreCampaign(saved))
        {
            campaignSaveBlocked_ = true;
            campaignMessage_ = NextDayz::L10n::FText("Cannot restore expedition: ") + saveError_;
        }
        else if (std::exchange(pendingDeployment_, false)) CompleteDeployment();
        else if (upgraded)
        {
            campaignMessage_ = "Region updated. Your gear and safehouse progress are preserved; previous world archived.";
            SaveCampaign();
        }
    }
    else if (exists)
    {
        NextDayz::FCampaignSave saved;
        bool migrated = false;
        bool retiredRegion = false;
        bool upgraded = false;
        const bool read = NextDayz::ReadCampaignSave(campaignPath_, saved, error);
        if (read && NextDayz::ReleaseTrainingDeployment(saved, expeditionMaps_))
            SPDLOG_INFO("[NextDayz] campaign was deployed into the training ground; returned it to the safehouse");
        const auto* savedMap = read ? NextDayz::FindExpeditionMap(expeditionMaps_,saved.map) : nullptr;
        if (savedMap && saved.mapVersion < savedMap->version)
        {
            const auto archive = std::filesystem::path(campaignPath_.string()+fmt::format(".before-{}-v{}",savedMap->id,saved.mapVersion));
            const bool archived = std::filesystem::exists(archive,ec);
            if (ec) error = "Cannot check map revision backup: " + ec.message();
            else if (!archived && !NextDayz::WriteCampaignSave(archive,saved,error))
                error = "Cannot preserve map revision backup: " + error;
            else if (savedMap->id == ActiveMapId())
            {
                auto target = *savedMap;
                target.scene = campaignMap_;
                upgraded = NextDayz::UpgradeExpeditionCampaign(saved,target,
                    {loot_.Capture(),time_.Capture(),target.version},player_.Position());
            }
        }
        if (read && (error.empty() || error.starts_with("Recovered backup")) && saved.map != campaignMap_)
        {
            const auto* currentMap = NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_);
            const std::string retired = NextDayz::RetiredExpeditionMapId(expeditionMaps_, saved.map);
            if (!authoringMode_ && currentMap && currentMap->published && !currentMap->training &&
                saved.map == NextDayz::LegacyRiverlandScene)
            {
                const auto archive = std::filesystem::path(campaignPath_.string() + ".riverland-backup");
                const bool archived = std::filesystem::exists(archive, ec);
                if (ec) error = "Cannot check Riverland backup: " + ec.message();
                else if (!archived && !NextDayz::WriteCampaignSave(archive, saved, error))
                    error = "Cannot preserve Riverland backup: " + error;
                else if (!NextDayz::MigrateRiverlandCampaign(saved, *currentMap,
                    {loot_.Capture(), time_.Capture(), currentMap->version}, player_.Position()))
                    error = "Cannot migrate Riverland progress; published map version differs";
                else migrated = true;
            }
            else if (!authoringMode_ && currentMap && currentMap->published && !currentMap->training && !retired.empty())
            {
                // The saved region was removed from the game: same rescue as Riverland.
                const auto archive = std::filesystem::path(campaignPath_.string() + fmt::format(".retired-{}-backup", retired));
                const bool archived = std::filesystem::exists(archive, ec);
                if (ec) error = "Cannot check retired region backup: " + ec.message();
                else if (!archived && !NextDayz::WriteCampaignSave(archive, saved, error))
                    error = "Cannot preserve retired region backup: " + error;
                else if (!NextDayz::RetireExpeditionCampaign(saved, *currentMap,
                    {loot_.Capture(), time_.Capture(), currentMap->version}, player_.Position(),
                    retired, "RETURNED / REGION RETIRED"))
                    error = "Cannot move progress out of a retired region; published map version differs";
                else
                {
                    migrated = true;
                    retiredRegion = true;
                }
            }
            else
            {
                const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, saved.map);
                if (map || saved.map == NextDayz::LegacyRiverlandScene)
                {
                    pendingCampaign_ = std::move(saved);
                    loadingMapPath_ = map && pendingCampaign_->mapVersion < map->version
                        ? (authoringMode_ ? map->draftScene : map->scene) : pendingCampaign_->map;
                    GetEngine().RequestLoadScene({.filename = loadingMapPath_});
                    return;
                }
                error = "Saved expedition map is unavailable";
            }
        }
        if (saved.map.empty() || (!error.empty() && !error.starts_with("Recovered backup")) || !RestoreCampaign(saved))
        {
            campaignSaveBlocked_ = true;
            campaignMessage_ = NextDayz::L10n::Message("Cannot load campaign. Original save preserved. {}", error.empty() ? saveError_ : error);
            saveError_ = campaignMessage_.english;
            SPDLOG_ERROR("[NextDayz] {}", saveError_);
        }
        else if (upgraded)
        {
            safehouseTab_ = 0;
            campaignMessage_ = "Region updated. Your gear and safehouse progress are preserved; previous world archived.";
            SaveCampaign();
        }
        else if (migrated)
        {
            safehouseTab_ = 0;
            campaignMessage_ = retiredRegion
                ? "That region has been retired. Your carried gear, storage and gold are safe. Accept a new world assignment."
                : "Riverland has been retired. Your carried gear, storage and gold are safe. Accept a new world assignment.";
            SPDLOG_INFO("[NextDayz] migrated {} campaign to {} safehouse; original archived",
                        retiredRegion ? "retired-region" : "Riverland", ActiveMapId());
            SaveCampaign();
        }
        else if (!error.empty()) campaignMessage_ = error;
    }
    else
    {
        campaign_ = {};
        mapStates_.clear();
        inventory_.Clear();
        survival_.Reset();
        campaignMessage_ = "Your safehouse is ready. Go in empty-handed, or prepare a loadout.";
        SaveCampaign();
    }
    if (!campaign_.inField || campaignSaveBlocked_) SetMouseCaptured(false);
    if (!campaignSaveBlocked_ && !pendingCampaign_ && RefreshContractBoard()) SaveCampaign();
    SPDLOG_INFO("[NextDayz] campaign {} ({})", campaignPath_.string(), campaign_.inField ? "field" : "safehouse");
}

NextDayz::FCampaignSave NextDayzGameInstance::CaptureCampaign() const
{
    if (cabinScene_ && cabinCampaign_)
    {
        auto saved = *cabinCampaign_;
        saved.campaign = campaign_;
        auto inventory = inventory_;
        weapons_.CopyMagazinesTo(inventory);
        saved.inventory = inventory.Capture();
        saved.weaponSlots = {weapons_.SlotInstance(0), weapons_.SlotInstance(1), weapons_.SlotInstance(2)};
        saved.activeSlot = weapons_.ActiveSlot();
        saved.previousSlot = weapons_.PreviousSlot();
        saved.sessionRng = sessionRng_.State();
        saved.survivalRng = survivalRng_.State();
        return saved;
    }
    NextDayz::FCampaignSave saved;
    saved.map = campaignMap_;
    saved.campaign = campaign_;
    auto inventory = inventory_;
    weapons_.CopyMagazinesTo(inventory);
    saved.inventory = inventory.Capture();
    saved.world = loot_.Capture();
    saved.survival = survival_.Snapshot();
    saved.time = time_.Capture();
    saved.position = player_.Position();
    saved.yaw = player_.Yaw();
    saved.pitch = player_.Pitch();
    saved.firstPerson = player_.PrefersFirstPerson();
    saved.weaponSlots = {weapons_.SlotInstance(0), weapons_.SlotInstance(1), weapons_.SlotInstance(2)};
    saved.activeSlot = weapons_.ActiveSlot();
    saved.previousSlot = weapons_.PreviousSlot();
    saved.survivalSeconds = survivalSeconds_;
    saved.sessionRng = sessionRng_.State();
    saved.survivalRng = survivalRng_.State();
    saved.quickSlots = quickSlots_;
    saved.lanternLit = lanternLit_;
    saved.lanternSeconds = lanternSeconds_;
    saved.hasCacheMarker = cacheMarker_.has_value();
    saved.cacheMarker = cacheMarker_.value_or(glm::vec3(0.0f));
    saved.cacheSeconds = cacheSeconds_;
    saved.maps = mapStates_;
    const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_);
    saved.mapVersion = map ? map->version : 1;
    saved.maps[ActiveMapId()] = {saved.world, saved.time, saved.mapVersion};
    return saved;
}

bool NextDayzGameInstance::SaveCampaign()
{
    if (!CampaignEnabled() || !sceneReady_ || switchingCampaignContext_ || pendingCampaign_ || campaignSaveBlocked_ || campaignPath_.empty()) return false;
    if (!NextDayz::WriteCampaignSave(campaignPath_, CaptureCampaign(), saveError_))
    {
        campaignSaveBlocked_ = true;
        campaignMessage_ = NextDayz::L10n::FText("Saving failed. Play paused to protect your progress: ") + saveError_;
        SetMouseCaptured(false);
        SPDLOG_ERROR("[NextDayz] {}", campaignMessage_);
        return false;
    }
    autosaveElapsed_ = 0.0f;
    return true;
}

bool NextDayzGameInstance::RestoreCampaign(const NextDayz::FCampaignSave& saved)
{
    if (saved.map != campaignMap_)
    {
        saveError_ = "Saved map does not match the current map";
        return false;
    }
    const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_);
    if (map && saved.mapVersion != map->version)
    {
        saveError_ = "Map version changed. Preserve the previous published map and save.";
        return false;
    }
    mapStates_ = saved.maps;
    if (!inventory_.Restore(saved.inventory)) return false;
    campaign_ = saved.campaign;
    loot_.Restore(saved.world, GetEngine());
    survival_.Restore(saved.survival);
    time_.Restore(saved.time);
    time_.Tick(0.0, GetEngine().GetScene());
    glm::vec3 restoredPosition = saved.position;
    if (authoringMode_)
    {
        auto clear = [this](const glm::vec3& feet)
        {
            bool blocked = false;
            const glm::vec3 waist = feet + glm::vec3(0,0.9f,0);
            for (const glm::vec3 direction : {glm::vec3(1,0,0),glm::vec3(-1,0,0),
                                              glm::vec3(0,0,1),glm::vec3(0,0,-1),glm::vec3(0,1,0)})
            {
                const float clearance = direction.y > 0 ? 0.8f : config_.Player.ControllerRadius;
                GetEngine().RayCast(waist,direction,[&](Assets::RayCastResult result)
                {
                    if (result.Hit && glm::distance(glm::vec3(result.HitPoint),waist) < clearance) blocked = true;
                    return true;
                });
            }
            return !blocked;
        };
        if (!clear(restoredPosition))
            for (const auto handle : world_.anchors.Find(NextDayz::EWorldAnchorType::PlayerSafe))
                if (const auto* spawn = world_.anchors.Resolve(handle); spawn && clear(spawn->worldPos))
                {
                    restoredPosition = spawn->worldPos;
                    SPDLOG_INFO("[NextDayz] edited geometry obstructed the authoring position; moved to safe spawn");
                    break;
                }
    }
    player_.Destroy();
    player_.Create(GetEngine().GetPhysicsEngine(), restoredPosition, config_);
    player_.SetYaw(saved.yaw);
    player_.SetPitch(saved.pitch);
    player_.SetFirstPerson(saved.firstPerson);
    weapons_.ResetRuntime();
    for (int slot = 0; slot < NextDayz::kWeaponSlotCount; ++slot)
        if (saved.weaponSlots[slot]) weapons_.EquipInstance(slot, inventory_, saved.weaponSlots[slot]);
    weapons_.RestoreSelection(saved.activeSlot, saved.previousSlot);
    survivalSeconds_ = saved.survivalSeconds;
    sessionRng_.RestoreState(saved.sessionRng);
    survivalRng_.RestoreState(saved.survivalRng);
    quickSlots_ = saved.quickSlots;
    lanternLit_ = saved.lanternLit;
    lanternSeconds_ = saved.lanternSeconds;
    cacheMarker_ = saved.hasCacheMarker ? std::optional(saved.cacheMarker) : std::nullopt;
    cacheSeconds_ = saved.cacheSeconds;
    actions_.Reset();
    showInventory_ = showMap_ = paused_ = false;
    SyncClothingVisuals();
    rescueHelicopter_.Update(GetEngine().GetScene(), campaign_.flight, extractionConfig_, 0.0f);
    SetMouseCaptured(campaign_.inField && !campaign_.flight.boarded);
    campaignMessage_ = campaign_.inField ? "Expedition resumed" : "Welcome back to your safehouse";
    return true;
}

void NextDayzGameInstance::Deploy()
{
    if (!CampaignEnabled() || campaignSaveBlocked_ || pendingCampaign_) return;
    if (cabinScene_ && cabinCampaign_ && campaign_.inField)
    {
        std::string targetScene = campaignMap_;
        if (const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_);
            map && cabinCampaign_->mapVersion < map->version)
        {
            targetScene = map->scene;
            pendingDeployment_ = true;
        }
        else if (campaignMap_ == NextDayz::LegacyRiverlandScene ||
            !NextDayz::RetiredExpeditionMapId(expeditionMaps_, campaignMap_).empty())
        {
            const auto replacement = std::find_if(expeditionMaps_.begin(), expeditionMaps_.end(),
                [](const auto& map) { return map.published && !map.training; });
            if (replacement == expeditionMaps_.end()) { campaignMessage_ = "No available assignment"; return; }
            targetScene = replacement->scene;
            pendingDeployment_ = true;
        }
        if (!std::filesystem::exists(Utilities::FileHelper::ResolvePlatformFilePath(targetScene.c_str())))
        {
            pendingDeployment_ = false;
            campaignMessage_ = "Assignment unavailable: scene files are missing";
            return;
        }
        pendingCampaign_ = CaptureCampaign();
        loadingMapPath_ = targetScene;
        loadingCabin_ = false;
        GetEngine().RequestLoadScene({.filename = loadingMapPath_});
        return;
    }
    if (campaign_.inField) return;
    if (!authoringMode_ && NextDayz::CanDeploy(campaign_, economy_) != NextDayz::ETradeResult::Ok)
    {
        // Extraction never destroys gear; an over-full warehouse is sorted before the next sortie.
        campaignMessage_ = NextDayz::L10n::Message(
            "Storage over capacity ({} / {} stacks). Sell, hand in or carry items, or expand the warehouse, before deploying.",
            campaign_.storage.size(), NextDayz::WarehouseCapacity(campaign_, economy_));
        return;
    }
    std::vector<const NextDayz::FExpeditionMap*> available;
    for (const auto& map : expeditionMaps_)
        if (NextDayz::IsDispatchable(map, authoringMode_)) available.push_back(&map);
    // Existing isolated extraction fixtures keep their explicit legacy scene.
    const bool legacyFixture = GOption->AgentValidation && validationDeployMap_.empty() &&
        !NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_);
    if (!legacyFixture && !available.empty())
    {
        // The draw always happens so a chosen destination leaves the RNG sequence unchanged.
        const auto* selected = available[sessionRng_.Range(static_cast<uint32_t>(available.size()))];
        if (const auto* chosen = ChosenDeployMap()) selected = chosen;
        if (authoringMode_) selected = NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_);
        if (GOption->AgentValidation && !validationDeployMap_.empty())
            selected = NextDayz::FindExpeditionMap(expeditionMaps_, validationDeployMap_);
        if (!selected) { campaignMessage_ = "No available assignment"; return; }
        if (selected->training && !authoringMode_)
        {
            campaignMessage_ = "The training ground is practice only. Enter it from the safehouse.";
            return;
        }
        const std::string& targetScene = authoringMode_ ? selected->draftScene : selected->scene;
        if (targetScene != campaignMap_ || cabinScene_)
        {
            RequestExpeditionMap(*selected, true);
            return;
        }
    }
    else if (!legacyFixture && !authoringMode_)
    {
        campaignMessage_ = "No published assignments available";
        return;
    }
    CompleteDeployment();
}

const NextDayz::FExpeditionMap* NextDayzGameInstance::ChosenDeployMap() const
{
    if (deployMapChoice_.empty() || authoringMode_) return nullptr;
    const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, deployMapChoice_);
    return map && NextDayz::IsDispatchable(*map, false) ? map : nullptr;
}

void NextDayzGameInstance::BrowseWorldMap(const NextDayz::FExpeditionMap& map)
{
    // Browsing a region in the offline campaign picks it as the next destination.
    if (!authoringMode_ && NextDayz::IsDispatchable(map, false)) SetDeployChoice(map.id);
}

void NextDayzGameInstance::SetDeployChoice(std::string mapId)
{
    deployMapChoice_ = std::move(mapId);
    if (campaign_.inField || campaign_.sortie.insertionMap.empty() || campaign_.sortie.insertionMap == deployMapChoice_)
        return;
    std::vector<std::function<void()>> refund{[this]() { NextDayz::CancelInsertion(campaign_); }};
    ApplySafehouseRequests(refund);
}

void NextDayzGameInstance::RequestExpeditionMap(const NextDayz::FExpeditionMap& map, bool deploy)
{
    if (pendingCampaign_ || campaignSaveBlocked_) return;
    if (map.training && !authoringMode_)
    {
        // Practice never carries the campaign: it is its own isolated session.
        StartTraining();
        return;
    }
    const std::string& targetScene = authoringMode_ ? map.draftScene : map.scene;
    if (!std::filesystem::exists(Utilities::FileHelper::ResolvePlatformFilePath(targetScene.c_str())))
    {
        campaignMessage_ = "Assignment unavailable: scene files are missing";
        return;
    }
    if (!SaveCampaign()) return;
    auto saved = CaptureCampaign();
    saved.map = targetScene;
    saved.mapVersion = map.version;
    auto state = saved.maps.find(map.id);
    if (state != saved.maps.end() && state->second.version < map.version)
    {
        saved.maps[fmt::format("{}@v{}",map.id,state->second.version)] = state->second;
        saved.maps.erase(map.id);
        state = saved.maps.end();
    }
    pendingFreshMap_ = state == saved.maps.end();
    if (!pendingFreshMap_)
    {
        saved.world = state->second.world;
        saved.time = state->second.time;
        saved.mapVersion = state->second.version;
    }
    saved.campaign.inField = false;
    saved.campaign.flight = {};
    pendingCampaign_ = std::move(saved);
    pendingDeployment_ = deploy;
    loadingMapPath_ = targetScene;
    campaignMessage_ = "Preparing classified deployment...";
    SetMouseCaptured(false);
    loadingCabin_ = false;
    GetEngine().RequestLoadScene({.filename = targetScene});
}

void NextDayzGameInstance::CompleteDeployment()
{
    const auto spawns = world_.anchors.Find(NextDayz::EWorldAnchorType::PlayerSafe);
    if (spawns.empty()) { campaignMessage_ = "This map has no safe deployment points"; return; }
    // The draw always happens so a paid landing leaves the RNG sequence unchanged.
    const auto* spawn = world_.anchors.Resolve(spawns[sessionRng_.Range(static_cast<uint32_t>(spawns.size()))]);
    lastInsertionError_ = -1.0f;
    if (const auto paid = NextDayz::TakeInsertion(campaign_, ActiveMapId()))
    {
        const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, ActiveMapId());
        static const std::vector<NextDayz::FLandingPoint> none;
        const auto& points = map ? LandingPoints(*map) : none;
        if (*paid < static_cast<int>(points.size()))
        {
            const glm::vec2 target = points[*paid].position;
            float best = std::numeric_limits<float>::max();
            for (const auto handle : spawns)
                if (const auto* candidate = world_.anchors.Resolve(handle))
                    if (const float distance = glm::distance(glm::vec2(candidate->worldPos.x, candidate->worldPos.z), target);
                        distance < best)
                    {
                        best = distance;
                        spawn = candidate;
                    }
            lastInsertionError_ = best;
        }
    }
    const glm::vec3 position = spawn->worldPos + glm::vec3(0.0f, 0.1f, 0.0f);
    player_.Destroy();
    player_.Create(GetEngine().GetPhysicsEngine(), position, config_);
    actions_.Reset();
    survival_.Reset();
    survivalSeconds_ = 0.0;
    StartNewLife();
    // New enemies can repopulate; physical loot and the world clock keep their state.
    world_.ResetPopulation(sessionRng_.NextU32());
    zombieVisuals_.Reset();
    SyncZombieVisuals();
    campaign_.inField = true;
    campaign_.flight = {};
    ++campaign_.sorties;
    ++campaign_.sortieId;
    ++campaign_.mapSorties[ActiveMapId()];
    PlantContractParcels();
    campaign_.lastMapId = ActiveMapId();
    campaign_.lastOutcome = "DEPLOYED";
    paused_ = showMap_ = showInventory_ = false;
    weapons_.SetTriggerDown(false);
    weapons_.SetPresentationSuppressed(false);
    SyncClothingVisuals();
    SetMouseCaptured(true);
    Notify("Find a flare gun, then call rescue at a marked helipad [B].", 0);
    SaveCampaign();
}

void NextDayzGameInstance::ReturnToSafehouse(ESafehouseReturn reason)
{
    const bool died = reason == ESafehouseReturn::Death;
    if (!campaign_.inField && died) return;
    NextDayz::FSortieRecord record;
    record.sortieId = campaign_.sortieId;
    record.mapId = ActiveMapId();
    record.outcome = died ? "died" : reason == ESafehouseReturn::Debug ? "debug" : "extracted";
    record.seconds = survivalSeconds_;
    if (died) record.deathCause = survival_.Snapshot().lastDamageSource;
    NextDayz::FContractReport contractReport;
    const uint32_t tierBefore = NextDayz::ReputationTier(campaign_.reputation, economy_);
    if (died)
    {
        weapons_.CopyMagazinesTo(inventory_);
        std::vector<NextDayz::FLootStack> contents;
        for (const auto& item : inventory_.Instances())
            contents.push_back({item.defId, item.count, item.loadedAmmo});
        if (!contents.empty() && !loot_.SpawnPile(std::move(contents), player_.Position(), player_.Yaw(), GetEngine()))
        {
            campaignSaveBlocked_ = true;
            campaignMessage_ = "Cannot preserve your dropped gear. Play paused.";
            SetMouseCaptured(false);
            return;
        }
        inventory_.Clear();
        campaignMessage_ = "You died. Your gear remains where you fell; you can try to recover it.";
    }
    else
    {
        weapons_.CopyMagazinesTo(inventory_);
        // Settle before anything expires: a parcel carried home on the last sortie still counts.
        // A debug return settles like an extraction so a parcel never reaches storage.
        contractReport = NextDayz::SettleContracts(campaign_, economy_, inventory_, ActiveMapId());
        record.extractedValue = NextDayz::SellValue(inventory_.Instances(), economy_);
        campaign_.StoreAll(inventory_);
        campaignMessage_ = reason == ESafehouseReturn::Debug
            ? "Debug return. Carried gear stored; extraction count unchanged."
            : "Extraction successful. Everything you carried is now in your safehouse.";
    }
    campaign_.inField = false;
    NextDayz::ExpireContracts(campaign_, economy_, contractReport);
    NextDayz::FinishSortie(campaign_, economy_, std::move(record));
    RefreshContractBoard();
    PurgeParcels(contractReport.purgeParcels);
    lastContractReport_ = contractReport;
    if (reason == ESafehouseReturn::Debug) debugger_.SetVisible(false);
    campaign_.lastOutcome = died ? "MISSING / GEAR LEFT IN FIELD"
        : reason == ESafehouseReturn::Debug ? "DEBUG RETURN / GEAR STORED" : "EXTRACTED";
    if (const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_))
        campaignMessage_ += NextDayz::L10n::Message(" Assignment region: {} / {}", map->name, map->region);
    campaignMessage_ += ContractReportText(contractReport) + TierUpText(tierBefore);
    if (NextDayz::WarehouseOver(campaign_, economy_))
        campaignMessage_ += NextDayz::L10n::Message(" Storage is over capacity ({} / {} stacks): sort it before the next sortie.",
            campaign_.storage.size(), NextDayz::WarehouseCapacity(campaign_, economy_));
    campaign_.flight = {};
    actions_.Reset();
    weapons_.ResetRuntime();
    survival_.Reset();
    StartNewLife();
    showInventory_ = showMap_ = paused_ = false;
    SyncClothingVisuals();
    rescueHelicopter_.Update(GetEngine().GetScene(), campaign_.flight, extractionConfig_, 0.0f);
    SetMouseCaptured(false);
    if (cabinLobby_) RequestCabin();
    else SaveCampaign();
}

void NextDayzGameInstance::DebugExpeditionAction(NextDayz::Debug::EExpeditionAction action)
{
    if (!sceneReady_ || !CampaignEnabled() || pendingCampaign_ || !loadingMapPath_.empty() || campaignSaveBlocked_)
        return;
    using NextDayz::Debug::EExpeditionAction;
    if (action == EExpeditionAction::Save)
    {
        if (SaveCampaign()) Notify("Campaign progress saved.", 0);
        return;
    }
    if (action == EExpeditionAction::ReloadMap)
    {
        if (SaveCampaign())
        {
            loadingMapPath_ = campaignMap_;
            GetEngine().RequestLoadScene({.filename = campaignMap_});
        }
        return;
    }
    if (!campaign_.inField) return;
    if (action == EExpeditionAction::ReturnToSafehouse)
    {
        ReturnToSafehouse(ESafehouseReturn::Debug);
        return;
    }
    if (action == EExpeditionAction::SafeSpawn)
        TeleportPlayer(ResolveSpawnPosition());
    else if (action == EExpeditionAction::ExtractionAlpha || action == EExpeditionAction::ExtractionBravo)
    {
        const auto pads = world_.anchors.Find(NextDayz::EWorldAnchorType::Extraction);
        const size_t index = action == EExpeditionAction::ExtractionAlpha ? 0 : 1;
        if (index >= pads.size()) return;
        if (const auto* pad = world_.anchors.Resolve(pads[index]))
            TeleportPlayer(pad->worldPos + glm::vec3(2,0.1f,0));
    }
    else if (action == EExpeditionAction::RescueSupplies)
    {
        GiveItem("signal_flare", 2);
        GiveItem("bandage", 2);
        GiveItem("water_bottle", 2);
    }
    SaveCampaign();
}

void NextDayzGameInstance::AdvanceExtraction(float dt)
{
    TrackContractSites(dt);
    const auto before = campaign_.flight.phase;
    if (campaign_.TickFlight(dt, extractionConfig_))
    {
        ReturnToSafehouse(ESafehouseReturn::Extraction);
        return;
    }
    rescueHelicopter_.Update(GetEngine().GetScene(), campaign_.flight, extractionConfig_, dt);
    if (campaign_.flight.phase != before)
    {
        Notify(campaign_.flight.phase == NextDayz::EFlightPhase::Waiting ? "Rescue landed. Go to the yellow door step [E]!"
               : campaign_.flight.phase == NextDayz::EFlightPhase::Idle ? "Rescue departed. Another flare is needed."
               : NextDayz::FlightPhaseName(campaign_.flight.phase), 0);
        SaveCampaign();
    }
    autosaveElapsed_ += dt;
    if (autosaveElapsed_ >= extractionConfig_.autosaveSeconds) SaveCampaign();
}

void NextDayzGameInstance::FireSignal(NextDayz::FItemInstanceId id)
{
    if (!CampaignEnabled() || !campaign_.inField || campaign_.flight.boarded || !survival_.IsAlive()) return;
    for (const auto& anchor : world_.anchors.All())
    {
        if (anchor.type != NextDayz::EWorldAnchorType::Extraction) continue;
        if (campaign_.Call(inventory_, id, anchor.profile, anchor.worldPos, player_.Position(), extractionConfig_))
        {
            world_.noise.Emit(player_.Position(), 120.0f, 1.0f, NextDayz::ENoiseType::Rifle);
            Notify("Signal fired. Rescue is on its way. Stay alive!", 3);
            SaveCampaign();
            return;
        }
    }
    Notify(campaign_.flight.phase != NextDayz::EFlightPhase::Idle ? "Rescue is already on its way"
                                                                 : "Fire the signal gun at a marked helipad", 1);
}

bool NextDayzGameInstance::BoardHelicopter()
{
    if (!survival_.IsAlive() || !campaign_.Board(player_.Position(), extractionConfig_)) return false;
    weapons_.SetTriggerDown(false);
    weapons_.SetPresentationSuppressed(true);
    player_.CancelAiming();
    rig_.SetVisible(false);
    SetInventoryOpen(false);
    Notify("On board. Taking off...", 3);
    SaveCampaign();
    return true;
}

std::string NextDayzGameInstance::ExtractionPrompt() const
{
    if (campaign_.flight.phase == NextDayz::EFlightPhase::Waiting &&
        glm::distance(player_.Position(), campaign_.flight.pad + glm::vec3(2.0f,0.3f,0)) < extractionConfig_.doorRadius)
        return "[E] Board rescue helicopter";
    if (campaign_.flight.phase != NextDayz::EFlightPhase::Idle)
        return fmt::format("{} | {:.0f}s", NextDayz::FlightPhaseName(campaign_.flight.phase), campaign_.flight.remaining);
    for (const auto& anchor : world_.anchors.All())
        if (anchor.type == NextDayz::EWorldAnchorType::Extraction &&
            glm::distance(anchor.worldPos, player_.Position()) <= extractionConfig_.callRadius)
            return fmt::format("{} | [B] Fire signal flare gun", anchor.label);
    return {};
}

bool NextDayzGameInstance::DrawSafehouse()
{
    if (!cabinScene_ && campaign_.inField && !campaignSaveBlocked_)
    {
        if (campaign_.flight.boarded)
        {
            ImGui::SetNextWindowPos(ImVec2(30,120), ImGuiCond_Always);
            ImGui::Begin(NextDayz::L10n::Label("Rescue"), nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text(NextDayz::L10n::Tr("On board - taking off: %.0fs"), campaign_.flight.remaining);
            ImGui::End();
        }
        return false;
    }
    SetMouseCaptured(false);
    weapons_.SetPresentationSuppressed(true);
    bool deploy = false;
    const auto records = NextDayz::L10n::Format("Gold: {}  |  Expeditions: {}  |  Extracted: {}",
        campaign_.gold, campaign_.sorties, campaign_.extractions);
    DrawCabinMenu(records + "\n" + campaignMessage_.Display(), !campaignSaveBlocked_ && !pendingCampaign_, campaign_.inField, deploy);
    if (deploy) Deploy();
    if (campaignSaveBlocked_)
    {
        BeginCabinPanel("CAMPAIGN UNAVAILABLE");
        ImGui::TextWrapped("%s", saveError_.c_str());
        if (ImGui::Button(NextDayz::L10n::Label("RETRY SAVE")) && !saveError_.empty() &&
            !campaignMessage_.starts_with("Cannot load"))
        { campaignSaveBlocked_ = false; SaveCampaign(); }
        ImGui::End();
        return true;
    }
    if (safehouseTab_ < 0) return true;
    BeginCabinPanel(safehouseTab_ == 0 ? "WORLD / OPERATIONS" : safehouseTab_ == 2 ? "TRADER / SERVICES" : "STORAGE / LOADOUT");
    if (campaign_.inField)
    {
        ImGui::TextWrapped("%s", NextDayz::L10n::Tr("Your expedition is saved. Continue when ready."));
        if (ImGui::Button(NextDayz::L10n::Label("CONTINUE EXPEDITION"))) Deploy();
        ImGui::End();
        return true;
    }
    if (safehouseTab_ == 0)
    {
        DrawWorldOperations();
        ImGui::End();
        return true;
    }
    if (safehouseTab_ == 2)
    {
        DrawTrader();
        ImGui::End();
        return true;
    }
    std::vector<std::function<void()>> requests;
    ImGui::Spacing();
    const auto storeResult = [this](NextDayz::ETradeResult result)
    { if (result != NextDayz::ETradeResult::Ok) campaignMessage_ = NextDayz::TradeResultMessage(result); };
    {
        const uint32_t capacity = NextDayz::WarehouseCapacity(campaign_, economy_);
        const bool over = NextDayz::WarehouseOver(campaign_, economy_);
        const auto used = NextDayz::L10n::Format("WAREHOUSE  {} / {} stacks", campaign_.storage.size(), capacity);
        if (over) ImGui::TextColored(ImVec4(0.92f,0.52f,0.40f,1), "%s", used.c_str());
        else ImGui::TextUnformatted(used.c_str());
        if (const auto* step = NextDayz::NextWarehouseStep(campaign_, economy_))
        {
            ImGui::SameLine();
            ImGui::BeginDisabled(campaign_.gold < step->price);
            if (ImGui::SmallButton(NextDayz::L10n::Format("Expand to {} stacks ({} gold)", step->capacity, step->price).c_str()))
                requests.emplace_back([this, storeResult]() { storeResult(NextDayz::UpgradeWarehouse(campaign_, economy_)); });
            ImGui::EndDisabled();
        }
        if (over)
            ImGui::TextColored(ImVec4(0.92f,0.52f,0.40f,1), "%s",
                NextDayz::L10n::Tr("Over capacity: you cannot deploy, buy or store until you sell, hand in or carry items."));
    }
    if (ImGui::Button(NextDayz::L10n::Label("STORE ALL CARRIED GEAR"))) requests.emplace_back([this, storeResult]()
    { storeResult(NextDayz::StoreCarried(campaign_, economy_, inventory_, 0)); weapons_.ResetRuntime(); });
    ImGui::SameLine();
    if (ImGui::Button(inventory_.Instances().empty() ? NextDayz::L10n::Tr("DEPLOY EMPTY-HANDED") : NextDayz::L10n::Tr("DEPLOY WITH LOADOUT")))
        requests.emplace_back([this]() { Deploy(); });
    ImGui::Separator();
    if (ImGui::BeginTable("storage_loadout", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable))
    {
        ImGui::TableNextColumn();
        ImGui::Text(NextDayz::L10n::Tr("STORAGE (%zu stacks)"), campaign_.storage.size());
        ImGui::BeginChild("storage", ImVec2(0, -12));
        for (const auto& item : campaign_.storage)
        {
            const auto* def = NextDayz::FindItemDef(item.defId);
            ImGui::PushID(static_cast<int>(item.instanceId));
            ImGui::Text("%s x%d", def ? std::string(NextDayz::L10n::Tr(def->displayName)).c_str() : item.defId.c_str(), item.count);
            if (item.loadedAmmo) { ImGui::SameLine(); ImGui::TextDisabled(NextDayz::L10n::Tr("(%d loaded)"), item.loadedAmmo); }
            const auto id = item.instanceId;
            if (ImGui::SmallButton(NextDayz::L10n::Label("Take 1"))) requests.emplace_back([this,id]()
            { if (!campaign_.Take(inventory_,id,1,false)) campaignMessage_ = "Not enough loadout space. Wear a larger pack first."; });
            ImGui::SameLine();
            if (def && def->kind == NextDayz::EItemKind::Clothing)
            {
                if (ImGui::SmallButton(NextDayz::L10n::Label("Wear"))) requests.emplace_back([this,id]()
                { if (!campaign_.Take(inventory_,id,1,true)) campaignMessage_ = "Cannot wear it; make room for the replaced clothing."; });
                ImGui::SameLine();
            }
            const int count = item.count;
            if (count > 1)
            {
                if (ImGui::SmallButton(NextDayz::L10n::Label("Take stack"))) requests.emplace_back([this,id,count]()
                { if (!campaign_.Take(inventory_,id,count,false)) campaignMessage_ = "Not enough loadout space."; });
                ImGui::SameLine();
            }
            const uint64_t price = NextDayz::UnitSellValue(item, economy_);
            ImGui::BeginDisabled(price == 0);
            if (ImGui::SmallButton(NextDayz::L10n::Format("Sell 1 (+{} gold)",price).c_str())) requests.emplace_back([this,id]()
            { campaign_.Sell(id,1,economy_); });
            ImGui::EndDisabled();
            if (count > 1)
            {
                ImGui::SameLine();
                if (price > 0 && ImGui::SmallButton(NextDayz::L10n::Label("Sell stack"))) requests.emplace_back([this,id,count]()
                { campaign_.Sell(id,count,economy_); });
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::Text(NextDayz::L10n::Tr("LOADOUT   Space: %d / %d"), inventory_.UsedCapacity(), inventory_.TotalCapacity());
        ImGui::BeginChild("loadout", ImVec2(0,-12));
        for (const auto& item : inventory_.Instances())
        {
            const auto* def = NextDayz::FindItemDef(item.defId);
            ImGui::PushID(static_cast<int>(item.instanceId));
            ImGui::Text("%s x%d%s", def ? std::string(NextDayz::L10n::Tr(def->displayName)).c_str() : item.defId.c_str(),
                item.count, inventory_.IsWorn(item.instanceId) ? NextDayz::L10n::Tr(" [worn]") : "");
            const auto id = item.instanceId;
            if (ImGui::SmallButton(NextDayz::L10n::Label("Store"))) requests.emplace_back([this,id]()
            {
                const auto result = NextDayz::StoreCarried(campaign_, economy_, inventory_, id);
                if (result == NextDayz::ETradeResult::StorageFull) campaignMessage_ = NextDayz::TradeResultMessage(result);
                else if (result != NextDayz::ETradeResult::Ok) campaignMessage_ = "Store its contents first, or use STORE ALL.";
            });
            if (def && def->kind == NextDayz::EItemKind::Clothing && !inventory_.IsWorn(id))
            {
                ImGui::SameLine();
                if (ImGui::SmallButton(NextDayz::L10n::Label("Wear"))) requests.emplace_back([this,id]() { WearItem(id); });
            }
            if (def && NextDayz::IsWieldable(*def))
            {
                ImGui::SameLine();
                if (ImGui::SmallButton(NextDayz::L10n::Label("Hold (1)"))) requests.emplace_back([this,id]() { EquipWeaponInstance(id,0); });
                ImGui::SameLine();
                if (ImGui::SmallButton(NextDayz::L10n::Label("Hold (2)"))) requests.emplace_back([this,id]() { EquipWeaponInstance(id,1); });
                if (NextDayz::WeaponSystem::CanEquipInSlot(NextDayz::kSidearmSlot, def->weaponDefId))
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(NextDayz::L10n::Label("Hold (3)"))) requests.emplace_back([this,id]() { EquipWeaponInstance(id,2); });
                }
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
    ImGui::End();
    ApplySafehouseRequests(requests);
    return true;
}

void NextDayzGameInstance::ApplySafehouseRequests(std::vector<std::function<void()>>& requests)
{
    if (requests.empty()) return;
    weapons_.CopyMagazinesTo(inventory_);
    const auto oldCampaign = campaign_;
    const auto oldInventory = inventory_;
    for (auto& request : requests) request();
    requests.clear();
    for (int slot = 0; slot < NextDayz::kWeaponSlotCount; ++slot)
        if (weapons_.SlotInstance(slot) && !inventory_.FindInstance(weapons_.SlotInstance(slot))) weapons_.Unequip(slot);
    SyncClothingVisuals();
    if (!campaign_.inField && !SaveCampaign())
    { campaign_ = oldCampaign; inventory_ = oldInventory; }
}

const std::vector<NextDayz::FLandingPoint>& NextDayzGameInstance::LandingPoints(const NextDayz::FExpeditionMap& map)
{
    if (const auto found = landingPoints_.find(map.id); found != landingPoints_.end()) return found->second;
    std::vector<NextDayz::FLandingPoint> points;
    std::string error;
    const auto path = NextDayz::GenerationRecordPath(map).generic_string();
    if (!map.training && !NextDayz::LoadLandingPoints(Utilities::FileHelper::ResolvePlatformFilePath(path.c_str()), points, error))
        SPDLOG_WARN("[NextDayz] no landing points for {}: {}", map.id, error);
    return landingPoints_.emplace(map.id, std::move(points)).first->second;
}

void NextDayzGameInstance::ConfigureCampaignCVars(NextCVar::FCVarSystem& cvars)
{
    cvars.RegisterString("nextdayz.validation.deployMap", "", &validationDeployMap_, NextCVar::ECVarFlags::None,
        "Agent-only deterministic map assignment; empty uses normal random dispatch");
    cvars.RegisterBool("nextdayz.authoring", false, &authoringMode_, NextCVar::ECVarFlags::None,
        "Use isolated authoring saves and enable draft map playtesting", [this]()
        {
            if (!sceneReady_ || !CampaignEnabled() || GOption->AgentValidation) return;
            if (pendingCampaign_ || campaignSaveBlocked_) { authoringMode_ = !authoringMode_; return; }
            // The previous context still owns campaignPath_. Save it before
            // loading a fresh scene into the other context's isolated save.
            if (!SaveCampaign()) { authoringMode_ = !authoringMode_; return; }
            switchingCampaignContext_ = true;
            loadingCabin_ = false;
            loadingMapPath_ = campaignMap_;
            GetEngine().RequestLoadScene({.filename = campaignMap_});
        });
    cvars.RegisterString("nextdayz.authoring.map", "", &authoringMapId_, NextCVar::ECVarFlags::None,
        "Playtest a draft map by stable id", [this]()
        {
            if (!authoringMode_ || !sceneReady_ || campaign_.inField) return;
            if (const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, authoringMapId_))
                RequestExpeditionMap(*map, true);
        });
    cvars.RegisterString("nextdayz.validation.campaignPath", validationCampaignPath_, &validationCampaignPath_,
        NextCVar::ECVarFlags::None, "Agent-only isolated campaign save path");
    cvars.RegisterBool("nextdayz.validation.campaignFresh", false, &validationCampaignFresh_,
        NextCVar::ECVarFlags::None, "Agent-only: start the next campaign initialization with a fresh fixture");
    cvars.RegisterBool("nextdayz.validation.campaign", false, &validationCampaign_, NextCVar::ECVarFlags::None,
        "Opt agent validation into the persistent campaign", [this]()
        { if (GOption->AgentValidation && validationCampaign_ && sceneReady_) InitializeCampaign(); });
    cvars.RegisterString("nextdayz.validation.campaignCommand", "", &validationCampaignCommand_,
        NextCVar::ECVarFlags::None, "Agent-only campaign actions", [this]()
        { if (GOption->AgentValidation && sceneReady_ && validationCampaign_) CampaignValidationCommand(); });
}

void NextDayzGameInstance::CampaignValidationCommand()
{
    const std::string command = std::exchange(validationCampaignCommand_, "");
    if (command == "new")
    {
        campaign_ = {};
        mapStates_.clear();
        inventory_.Clear();
        survival_.Reset();
        weapons_.ResetRuntime();
        loot_.ResetSession(GetEngine());
        campaignSaveBlocked_ = false;
        saveError_.clear();
        RefreshContractBoard();
        SaveCampaign();
        SetMouseCaptured(false);
    }
    else if (command == "deploy") Deploy();
    else if (command == "save") SaveCampaign();
    else if (command == "rebuild")
    {
        loadingCabin_ = cabinScene_;
        GetEngine().RequestLoadScene({.filename = cabinScene_ ? cabinScenePath : campaignMap_});
    }
    else if (command == "reload")
    {
        NextDayz::FCampaignSave saved;
        std::string error;
        if (!NextDayz::ReadCampaignSave(campaignPath_,saved,error) || !RestoreCampaign(saved))
        { saveError_ = error; campaignSaveBlocked_ = true; }
    }
    else if (command == "store-all" && !campaign_.inField)
    {
        const auto result = NextDayz::StoreCarried(campaign_, economy_, inventory_, 0);
        if (result != NextDayz::ETradeResult::Ok) campaignMessage_ = NextDayz::TradeResultMessage(result);
        SaveCampaign();
    }
    else if (command.starts_with("gold:")) { campaign_.gold = std::stoull(command.substr(5)); SaveCampaign(); }
    else if (command.starts_with("fill-storage:") && !campaign_.inField)
    {
        // Adds stacks that cannot merge (one garment each, cycling through clothing) past any capacity.
        int stacks = std::stoi(command.substr(13));
        for (size_t index = 0; stacks > 0; ++index)
        {
            const auto& item = NextDayz::AllItemDefs()[index % NextDayz::AllItemDefs().size()];
            if (item.kind != NextDayz::EItemKind::Clothing || item.maxStack != 1) continue;
            NextDayz::FItemInstance stack;
            stack.defId = std::string(item.id);
            campaign_.Put(std::move(stack));
            --stacks;
        }
        SaveCampaign();
    }
    else if (command == "warehouse-upgrade")
    {
        campaignMessage_ = NextDayz::TradeResultMessage(NextDayz::UpgradeWarehouse(campaign_, economy_));
        SaveCampaign();
    }
    else if (command == "take-first" && !campaign_.storage.empty())
    { campaign_.Take(inventory_,campaign_.storage.front().instanceId,1,false); SaveCampaign(); }
    else if (command == "sell-first" && !campaign_.storage.empty())
    { campaign_.Sell(campaign_.storage.front().instanceId,1,economy_); SaveCampaign(); }
    else if (command == "sell-all" && !campaign_.inField)
    {
        while (!campaign_.storage.empty() &&
               campaign_.Sell(campaign_.storage.front().instanceId, campaign_.storage.front().count, economy_)) {}
        SaveCampaign();
    }
    else if (command.starts_with("buy:"))
    {
        campaignMessage_ = NextDayz::TradeResultMessage(NextDayz::Buy(campaign_, economy_, command.substr(4)));
        SaveCampaign();
    }
    else if (command.starts_with("insert:"))
    {
        // Pays for a landing point on the deployment map (validation map or chosen destination).
        const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_,
            validationDeployMap_.empty() ? deployMapChoice_ : validationDeployMap_);
        const int point = std::stoi(command.substr(7));
        if (map && point >= 0 && point < static_cast<int>(LandingPoints(*map).size()))
        {
            if (deployMapChoice_ != map->id) SetDeployChoice(map->id);
            campaignMessage_ = NextDayz::TradeResultMessage(NextDayz::PayInsertion(campaign_, economy_, map->id, point));
            SaveCampaign();
        }
    }
    else if (command == "insert-cancel") { NextDayz::CancelInsertion(campaign_); SaveCampaign(); }
    else if (command.starts_with("reputation:")) { campaign_.reputation = static_cast<uint32_t>(std::stoul(command.substr(11))); SaveCampaign(); }
    else if (command.starts_with("contract-offer:"))
    {
        if (!NextDayz::OfferContract(campaign_, economy_, ContractSites(), command.substr(15)))
            SPDLOG_WARN("[NextDayz] validation could not offer contract {}", command.substr(15));
        SaveCampaign();
    }
    else if (command == "contract-reroll" && !campaign_.inField)
    {
        campaign_.board.clear();
        campaign_.boardCycle = 0;
        RefreshContractBoard();
        SaveCampaign();
    }
    else if (command.starts_with("contract-accept:") || command.starts_with("contract-abandon:"))
    {
        // The first offered (accept) or accepted (abandon) contract of the given type.
        const bool accept = command.starts_with("contract-accept:");
        const std::string type = command.substr(command.find(':') + 1);
        const auto& list = accept ? campaign_.board : campaign_.contracts;
        const auto found = std::find_if(list.begin(), list.end(),
            [&](const auto& contract) { return NextDayz::ContractTypeName(contract.type) == type; });
        if (found != list.end())
        {
            const uint64_t id = found->id;
            const std::string parcel = found->parcel;
            NextDayz::FContractReport report;
            const auto result = accept ? NextDayz::AcceptContract(campaign_, economy_, id)
                                       : NextDayz::AbandonContract(campaign_, economy_, id, report);
            if (result == NextDayz::EContractResult::Ok && !parcel.empty()) PurgeParcels({parcel});
            campaignMessage_ = NextDayz::ContractResultMessage(result);
            lastContractReport_ = report;
            SaveCampaign();
        }
    }
    else if ((command == "contract-fill" || command == "contract-handin") && !campaign_.inField)
    {
        const auto found = std::find_if(campaign_.board.begin(), campaign_.board.end(),
            [](const auto& contract) { return contract.type == NextDayz::EContractType::Deliver; });
        if (found != campaign_.board.end())
        {
            if (command == "contract-fill")
                for (const auto& item : found->items) NextDayz::AddToStorage(campaign_, item.item, item.count);
            else
            {
                NextDayz::FContractReport report;
                campaignMessage_ = NextDayz::ContractResultMessage(NextDayz::HandInContract(campaign_, economy_, found->id, report));
                lastContractReport_ = report;
            }
            SaveCampaign();
        }
    }
    else if (command == "goto-target" && campaign_.inField)
    {
        // The next place the first contract on this map needs: an unvisited site, or its parcel.
        for (const auto& contract : campaign_.contracts)
        {
            if (contract.mapId != ActiveMapId()) continue;
            if (contract.type == NextDayz::EContractType::Fetch)
            {
                if (const auto at = loot_.FindItem(contract.parcel)) { TeleportPlayer(*at + glm::vec3(0.0f, 0.1f, 0.0f)); break; }
                continue;
            }
            const auto& visited = campaign_.sortie.visitedPois;
            const auto target = std::find_if(contract.targets.begin(), contract.targets.end(), [&](const auto& site)
            { return std::find(visited.begin(), visited.end(), site) == visited.end(); });
            if (target == contract.targets.end()) continue;
            for (const auto& anchor : world_.anchors.All())
                if (anchor.type == NextDayz::EWorldAnchorType::Poi && anchor.label == *target)
                {
                    TeleportPlayer(anchor.worldPos + glm::vec3(0.0f, 0.1f, 0.0f));
                    break;
                }
            break;
        }
    }
    else if (command.starts_with("take-item:"))
    {
        const std::string item = command.substr(10);
        for (size_t index = 0; index < loot_.Entries().size(); ++index)
        {
            const auto& entry = loot_.Entries()[index];
            if (entry.state != NextDayz::FLootEntry::EState::Available ||
                std::none_of(entry.contents.begin(), entry.contents.end(), [&](const auto& stack) { return stack.itemId == item; }))
                continue;
            if (const auto handle = loot_.ReserveEntry(static_cast<int>(index))) loot_.Commit(*handle, inventory_, GetEngine());
            break;
        }
        SaveCampaign();
    }
    else if (command.starts_with("page:")) { traderPage_ = std::stoi(command.substr(5)); traderPageForced_ = true; }
    else if (command == "call")
    { if (const auto* flare = inventory_.FindFirst("signal_flare")) FireSignal(flare->instanceId); }
    else if (command == "board") BoardHelicopter();
    else if (command.starts_with("pad:"))
    {
        const auto pads = world_.anchors.Find(NextDayz::EWorldAnchorType::Extraction);
        const auto index = static_cast<size_t>(std::stoul(command.substr(4)));
        if (index < pads.size())
        {
            const auto* pad = world_.anchors.Resolve(pads[index]);
            player_.Destroy();
            player_.Create(GetEngine().GetPhysicsEngine(),pad->worldPos + glm::vec3(2,0.1f,0),config_);
        }
    }
    else if (command.starts_with("flight:")) AdvanceExtraction(std::stof(command.substr(7)));
    else if (command == "pile-stress")
    {
        for (int index = 0; index < 40; ++index)
            loot_.SpawnPile({{"bandage",1,0}}, player_.Position() + glm::vec3(index % 8,0,index / 8),0,GetEngine());
        SaveCampaign();
    }
    else if (command == "recover-pile")
    {
        for (size_t index = 0; index < loot_.Entries().size(); ++index)
        {
            const auto& entry = loot_.Entries()[index];
            if (entry.source != NextDayz::ELootSource::Pile || entry.state != NextDayz::FLootEntry::EState::Available) continue;
            if (const auto handle = loot_.ReserveEntry(static_cast<int>(index)))
                loot_.Commit(*handle,inventory_,GetEngine());
            break;
        }
        SaveCampaign();
    }
    else if (command == "take-authored")
    {
        for (size_t index = 0; index < loot_.Entries().size(); ++index)
        {
            const auto& entry = loot_.Entries()[index];
            if (entry.source != NextDayz::ELootSource::Authored || entry.state != NextDayz::FLootEntry::EState::Available ||
                entry.contents.size() != 1 || entry.contents.front().itemId != "food_can") continue;
            if (const auto handle = loot_.ReserveEntry(static_cast<int>(index)))
                loot_.Commit(*handle,inventory_,GetEngine());
            break;
        }
        SaveCampaign();
    }
}
