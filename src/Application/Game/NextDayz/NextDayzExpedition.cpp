#include "Engine/Common/CoreMinimal.hpp"
#include "NextDayzGameInstance.hpp"

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
    return !GOption->AgentValidation || validationCampaign_;
}

std::string NextDayzGameInstance::ActiveMapId() const
{
    const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_);
    return map ? map->id : campaignMap_;
}

void NextDayzGameInstance::InitializeCampaign()
{
    if (!CampaignEnabled()) return;
    switchingCampaignContext_ = false;
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
            campaignMessage_ = "Cannot restore expedition: " + saveError_;
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
        bool upgraded = false;
        const bool read = NextDayz::ReadCampaignSave(campaignPath_, saved, error);
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
            if (!authoringMode_ && currentMap && currentMap->published && saved.map == NextDayz::LegacyRiverlandScene)
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
            saveError_ = "Cannot load campaign. Original save preserved. " + (error.empty() ? saveError_ : error);
            campaignMessage_ = saveError_;
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
            campaignMessage_ = "Riverland has been retired. Your carried gear, storage and gold are safe. Accept a new world assignment.";
            SPDLOG_INFO("[NextDayz] migrated Riverland campaign to {} safehouse; original archived", ActiveMapId());
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
    SPDLOG_INFO("[NextDayz] campaign {} ({})", campaignPath_.string(), campaign_.inField ? "field" : "safehouse");
}

NextDayz::FCampaignSave NextDayzGameInstance::CaptureCampaign() const
{
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
    saved.firstPerson = player_.IsFirstPerson();
    saved.weaponSlots = {weapons_.SlotInstance(0), weapons_.SlotInstance(1)};
    saved.activeSlot = weapons_.ActiveSlot();
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
    if (!CampaignEnabled() || !sceneReady_ || pendingCampaign_ || campaignSaveBlocked_ || campaignPath_.empty()) return false;
    if (!NextDayz::WriteCampaignSave(campaignPath_, CaptureCampaign(), saveError_))
    {
        campaignSaveBlocked_ = true;
        campaignMessage_ = "Saving failed. Play paused to protect your progress: " + saveError_;
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
            for (const auto handle : worldAnchors_.Find(NextDayz::EWorldAnchorType::PlayerSafe))
                if (const auto* spawn = worldAnchors_.Resolve(handle); spawn && clear(spawn->worldPos))
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
    for (int slot = 0; slot < 2; ++slot)
        if (saved.weaponSlots[slot]) weapons_.EquipInstance(slot, inventory_, saved.weaponSlots[slot]);
    weapons_.SwitchSlot(saved.activeSlot);
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
    if (!CampaignEnabled() || campaign_.inField || campaignSaveBlocked_) return;
    if (pendingCampaign_) return;
    std::vector<const NextDayz::FExpeditionMap*> available;
    for (const auto& map : expeditionMaps_)
        if (map.published || authoringMode_) available.push_back(&map);
    // Existing isolated extraction fixtures keep their explicit legacy scene.
    const bool legacyFixture = GOption->AgentValidation && validationDeployMap_.empty() &&
        !NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_);
    if (!legacyFixture && !available.empty())
    {
        const auto* selected = available[sessionRng_.Range(static_cast<uint32_t>(available.size()))];
        if (authoringMode_) selected = NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_);
        if (GOption->AgentValidation && !validationDeployMap_.empty())
            selected = NextDayz::FindExpeditionMap(expeditionMaps_, validationDeployMap_);
        if (!selected) { campaignMessage_ = "No available assignment"; return; }
        const std::string& targetScene = authoringMode_ ? selected->draftScene : selected->scene;
        if (targetScene != campaignMap_)
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

void NextDayzGameInstance::RequestExpeditionMap(const NextDayz::FExpeditionMap& map, bool deploy)
{
    if (pendingCampaign_ || campaignSaveBlocked_) return;
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
    GetEngine().RequestLoadScene({.filename = targetScene});
}

void NextDayzGameInstance::CompleteDeployment()
{
    const auto spawns = worldAnchors_.Find(NextDayz::EWorldAnchorType::PlayerSafe);
    if (spawns.empty()) { campaignMessage_ = "This map has no safe deployment points"; return; }
    const auto* spawn = worldAnchors_.Resolve(spawns[sessionRng_.Range(static_cast<uint32_t>(spawns.size()))]);
    const glm::vec3 position = spawn->worldPos + glm::vec3(0.0f, 0.1f, 0.0f);
    player_.Destroy();
    player_.Create(GetEngine().GetPhysicsEngine(), position, config_);
    actions_.Reset();
    survival_.Reset();
    survivalSeconds_ = 0.0;
    StartNewLife();
    // New enemies can repopulate; physical loot and the world clock keep their state.
    zombies_.Reset();
    zombieVisuals_.Reset();
    combat_.Reset();
    noise_.Reset();
    zombieSpawns_.Reset(sessionRng_.NextU32());
    ConfigureZombieSpawnPoints();
    SyncZombieVisuals();
    campaign_.inField = true;
    campaign_.flight = {};
    ++campaign_.sorties;
    ++campaign_.sortieId;
    ++campaign_.mapSorties[ActiveMapId()];
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
        campaign_.StoreAll(inventory_);
        campaignMessage_ = reason == ESafehouseReturn::Debug
            ? "Debug return. Carried gear stored; extraction count unchanged."
            : "Extraction successful. Everything you carried is now in your safehouse.";
    }
    campaign_.inField = false;
    if (reason == ESafehouseReturn::Debug) debugger_.SetVisible(false);
    campaign_.lastOutcome = died ? "MISSING / GEAR LEFT IN FIELD"
        : reason == ESafehouseReturn::Debug ? "DEBUG RETURN / GEAR STORED" : "EXTRACTED";
    if (const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_))
        campaignMessage_ += " Assignment region: " + map->name + " / " + map->region;
    campaign_.flight = {};
    actions_.Reset();
    weapons_.ResetRuntime();
    survival_.Reset();
    StartNewLife();
    showInventory_ = showMap_ = paused_ = false;
    SyncClothingVisuals();
    rescueHelicopter_.Update(GetEngine().GetScene(), campaign_.flight, extractionConfig_, 0.0f);
    SetMouseCaptured(false);
    SaveCampaign();
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
        const auto pads = worldAnchors_.Find(NextDayz::EWorldAnchorType::Extraction);
        const size_t index = action == EExpeditionAction::ExtractionAlpha ? 0 : 1;
        if (index >= pads.size()) return;
        if (const auto* pad = worldAnchors_.Resolve(pads[index]))
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
    for (const auto& anchor : worldAnchors_.All())
    {
        if (anchor.type != NextDayz::EWorldAnchorType::Extraction) continue;
        if (campaign_.Call(inventory_, id, anchor.profile, anchor.worldPos, player_.Position(), extractionConfig_))
        {
            noise_.Emit(player_.Position(), 120.0f, 1.0f, NextDayz::ENoiseType::Rifle);
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
    player_.SetAiming(false);
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
    for (const auto& anchor : worldAnchors_.All())
        if (anchor.type == NextDayz::EWorldAnchorType::Extraction &&
            glm::distance(anchor.worldPos, player_.Position()) <= extractionConfig_.callRadius)
            return fmt::format("{} | [B] Fire signal flare gun", anchor.label);
    return {};
}

bool NextDayzGameInstance::DrawSafehouse()
{
    if (campaign_.inField && !campaignSaveBlocked_)
    {
        if (campaign_.flight.boarded)
        {
            ImGui::SetNextWindowPos(ImVec2(30,120), ImGuiCond_Always);
            ImGui::Begin("Rescue", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("On board - taking off: %.0fs", campaign_.flight.remaining);
            ImGui::End();
        }
        return false;
    }
    SetMouseCaptured(false);
    weapons_.SetPresentationSuppressed(true);
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowBgAlpha(0.97f);
    ImGui::Begin("NEXTDAYZ / SAFEHOUSE", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("SAFEHOUSE");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::SameLine();
    ImGui::Text("  Gold: %llu   |   Expeditions: %llu   |   Extracted: %llu",
        static_cast<unsigned long long>(campaign_.gold), static_cast<unsigned long long>(campaign_.sorties),
        static_cast<unsigned long long>(campaign_.extractions));
    ImGui::TextWrapped("%s", campaignMessage_.c_str());
    ImGui::TextDisabled("Sell surplus supplies to clear storage and build your gold reserves.");
    if (campaignSaveBlocked_)
    {
        ImGui::TextWrapped("%s", saveError_.c_str());
        if (ImGui::Button("RETRY SAVE") && !saveError_.empty())
        {
            // A load failure must never overwrite the unreadable original.
            if (!campaignMessage_.starts_with("Cannot load"))
            { campaignSaveBlocked_ = false; SaveCampaign(); }
        }
        ImGui::End();
        return true;
    }
    ImGui::Spacing();
    if (ImGui::Button("WORLD / OPERATIONS", ImVec2(210, 36))) safehouseTab_ = 0;
    ImGui::SameLine();
    if (ImGui::Button("STORAGE / LOADOUT", ImVec2(210, 36))) safehouseTab_ = 1;
    ImGui::Separator();
    if (safehouseTab_ == 0)
    {
        DrawWorldOperations();
        ImGui::End();
        return true;
    }
    std::vector<std::function<void()>> requests;
    ImGui::Spacing();
    if (ImGui::Button("STORE ALL CARRIED GEAR")) requests.emplace_back([this]()
    { campaign_.StoreAll(inventory_); weapons_.ResetRuntime(); });
    ImGui::SameLine();
    if (ImGui::Button(inventory_.Instances().empty() ? "DEPLOY EMPTY-HANDED" : "DEPLOY WITH LOADOUT"))
        requests.emplace_back([this]() { Deploy(); });
    ImGui::Separator();
    if (ImGui::BeginTable("storage_loadout", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable))
    {
        ImGui::TableNextColumn();
        ImGui::Text("STORAGE (%zu stacks)", campaign_.storage.size());
        ImGui::BeginChild("storage", ImVec2(0, -12));
        for (const auto& item : campaign_.storage)
        {
            const auto* def = NextDayz::FindItemDef(item.defId);
            ImGui::PushID(static_cast<int>(item.instanceId));
            ImGui::Text("%s x%d", def ? std::string(def->displayName).c_str() : item.defId.c_str(), item.count);
            if (item.loadedAmmo) { ImGui::SameLine(); ImGui::TextDisabled("(%d loaded)", item.loadedAmmo); }
            const auto id = item.instanceId;
            if (ImGui::SmallButton("Take 1")) requests.emplace_back([this,id]()
            { if (!campaign_.Take(inventory_,id,1,false)) campaignMessage_ = "Not enough loadout space. Wear a larger pack first."; });
            ImGui::SameLine();
            if (def && def->kind == NextDayz::EItemKind::Clothing)
            {
                if (ImGui::SmallButton("Wear")) requests.emplace_back([this,id]()
                { if (!campaign_.Take(inventory_,id,1,true)) campaignMessage_ = "Cannot wear it; make room for the replaced clothing."; });
                ImGui::SameLine();
            }
            const int count = item.count;
            if (count > 1)
            {
                if (ImGui::SmallButton("Take stack")) requests.emplace_back([this,id,count]()
                { if (!campaign_.Take(inventory_,id,count,false)) campaignMessage_ = "Not enough loadout space."; });
                ImGui::SameLine();
            }
            const int price = NextDayz::SellPrice(item.defId, extractionConfig_);
            if (ImGui::SmallButton(fmt::format("Sell 1 (+{} gold)",price).c_str())) requests.emplace_back([this,id]()
            { campaign_.Sell(id,1,extractionConfig_); });
            if (count > 1)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Sell stack")) requests.emplace_back([this,id,count]()
                { campaign_.Sell(id,count,extractionConfig_); });
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::Text("LOADOUT   Space: %d / %d", inventory_.UsedCapacity(), inventory_.TotalCapacity());
        ImGui::BeginChild("loadout", ImVec2(0,-12));
        for (const auto& item : inventory_.Instances())
        {
            const auto* def = NextDayz::FindItemDef(item.defId);
            ImGui::PushID(static_cast<int>(item.instanceId));
            ImGui::Text("%s x%d%s", def ? std::string(def->displayName).c_str() : item.defId.c_str(),
                item.count, inventory_.IsWorn(item.instanceId) ? " [worn]" : "");
            const auto id = item.instanceId;
            if (ImGui::SmallButton("Store")) requests.emplace_back([this,id]()
            { if (!campaign_.Store(inventory_,id)) campaignMessage_ = "Store its contents first, or use STORE ALL."; });
            if (def && def->kind == NextDayz::EItemKind::Clothing && !inventory_.IsWorn(id))
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Wear")) requests.emplace_back([this,id]() { WearItem(id); });
            }
            if (def && NextDayz::IsWieldable(*def))
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Hold (1)")) requests.emplace_back([this,id]() { EquipWeaponInstance(id,0); });
                ImGui::SameLine();
                if (ImGui::SmallButton("Hold (2)")) requests.emplace_back([this,id]() { EquipWeaponInstance(id,1); });
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
    ImGui::End();
    if (!requests.empty())
    {
        weapons_.CopyMagazinesTo(inventory_);
        const auto oldCampaign = campaign_;
        const auto oldInventory = inventory_;
        for (auto& request : requests) request();
        for (int slot = 0; slot < 2; ++slot)
            if (weapons_.SlotInstance(slot) && !inventory_.FindInstance(weapons_.SlotInstance(slot))) weapons_.Unequip(slot);
        SyncClothingVisuals();
        if (!campaign_.inField && !SaveCampaign())
        { campaign_ = oldCampaign; inventory_ = oldInventory; }
    }
    return true;
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
        SaveCampaign();
        SetMouseCaptured(false);
    }
    else if (command == "deploy") Deploy();
    else if (command == "save") SaveCampaign();
    else if (command == "rebuild") GetEngine().RequestLoadScene({.filename = campaignMap_});
    else if (command == "reload")
    {
        NextDayz::FCampaignSave saved;
        std::string error;
        if (!NextDayz::ReadCampaignSave(campaignPath_,saved,error) || !RestoreCampaign(saved))
        { saveError_ = error; campaignSaveBlocked_ = true; }
    }
    else if (command == "store-all" && !campaign_.inField) { campaign_.StoreAll(inventory_); SaveCampaign(); }
    else if (command == "take-first" && !campaign_.storage.empty())
    { campaign_.Take(inventory_,campaign_.storage.front().instanceId,1,false); SaveCampaign(); }
    else if (command == "sell-first" && !campaign_.storage.empty())
    { campaign_.Sell(campaign_.storage.front().instanceId,1,extractionConfig_); SaveCampaign(); }
    else if (command == "call")
    { if (const auto* flare = inventory_.FindFirst("signal_flare")) FireSignal(flare->instanceId); }
    else if (command == "board") BoardHelicopter();
    else if (command.starts_with("pad:"))
    {
        const auto pads = worldAnchors_.Find(NextDayz::EWorldAnchorType::Extraction);
        const auto index = static_cast<size_t>(std::stoul(command.substr(4)));
        if (index < pads.size())
        {
            const auto* pad = worldAnchors_.Resolve(pads[index]);
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
