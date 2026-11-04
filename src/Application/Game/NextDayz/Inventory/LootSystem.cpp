#include "LootSystem.hpp"

#include <algorithm>
#include <limits>
#include <numeric>

#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"

#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"

namespace NextDayz
{
    namespace
    {
        constexpr size_t kPileCount = 32;
        constexpr float kZoneLinkDistance = 28.0f;
        constexpr float kRestockInterval = 1.0f;
        constexpr float kInitialStockShare = 0.78f;
        constexpr float kLootParkedY = -1000.0f;
        // Restocking never fills more than this share of all entries with one category.
        constexpr std::array<float, static_cast<size_t>(ELootCategory::Count)> kCategoryStockCap = {
            0.45f, 0.20f, 0.14f, 0.08f, 0.15f, 0.25f};

        // Map nodes the artist placed as specific loot. Fixed items are always
        // there; rolled ones come from a profile table each time it restocks.
        struct FAuthoredLoot
        {
            std::string_view displayName;
            std::array<std::string_view, 2> fixed{};
            ELootProfile rollProfile = ELootProfile::Military;
            ELootCategory rollCategory = ELootCategory::Count;
            int rolls = 0;
        };

        const std::unordered_map<std::string, FAuthoredLoot>& AuthoredTable()
        {
            using P = ELootProfile;
            using C = ELootCategory;
            static const std::unordered_map<std::string, FAuthoredLoot> table = {
                {"cw_wpn_ak",            {"AK-74", {"ak"}}},
                {"cw_wpn_svd",           {"SVD", {"svd"}}},
                {"cw_wpn_mosin",         {"Mosin", {"mosin"}}},
                {"cw_wpn_shotgun",       {"Shotgun", {"shotgun"}}},
                {"cw_wpn_pistol",        {"Makarov", {"pistol"}}},
                {"cw_wpn_crate",         {"Weapon Crate", {"ak"}, P::Military, C::Ammo, 2}},
                {"cw_prop_crate_ammo",   {"Ammo Crate", {}, P::Military, C::Ammo, 2}},
                {"cw_item_ammobox",      {"Ammo Box", {}, P::Military, C::Ammo, 1}},
                {"cw_item_helmet",       {"Helmet", {"helmet"}}},
                {"cw_item_backpack",     {"Backpack", {"backpack"}}},
                {"cw_item_medkit",       {"First Aid Kit", {"medkit"}}},
                {"cw_item_can",          {"Canned Beans", {"food_can"}}},
                {"cw_item_jerrycan",     {"Fuel Canister", {"fuel"}}},
                {"cw_item_radio",        {"Field Radio", {"radio", "battery"}}},
                {"cw_item_crate_supply", {"Supply Crate", {}, P::Commercial, C::Count, 3}},
                {"cw_item_bedroll",      {"Bedroll", {"bedroll"}}},
                {"cw_item_lantern",      {"Kerosene Lantern", {"lantern"}}},
                {"cw_item_water_bottle", {"Water Bottle", {"water_bottle"}}},
                {"cw_item_axe",          {"Fire Axe", {"axe"}}},
                {"cw_item_crowbar",      {"Crowbar", {"crowbar"}}},
                {"cw_item_knife",        {"Hunting Knife", {"knife"}}},
                {"cw_item_vest",         {"Military Vest", {"vest_military"}}},
                {"cw_item_jacket",       {"Field Jacket", {"jacket"}}},
                {"cw_item_medical_bag",  {"Medical Bag", {}, P::Medical, C::Medical, 3}},
                {"cw_item_toolbox",      {"Toolbox", {}, P::Industrial, C::Count, 2}},
            };
            return table;
        }

        std::vector<std::pair<std::string, const FAuthoredLoot*>> AuthoredByIndex()
        {
            std::vector<std::pair<std::string, const FAuthoredLoot*>> list;
            for (const auto& [name, def] : AuthoredTable())
            {
                list.emplace_back(name, &def);
            }
            std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            return list;
        }

        void SetNodeTreeVisible(Assets::Node* node, NextPhysics* physics, bool visible)
        {
            if (!node)
            {
                return;
            }
            if (auto render = node->GetComponent<Runtime::RenderComponent>())
            {
                render->SetVisible(visible);
            }
            if (auto phys = node->GetComponent<Runtime::PhysicsComponent>(); phys && physics)
            {
                physics->SetBodyActive(phys->GetPhysicsBody(), visible);
            }
            for (const auto& child : node->Children())
            {
                SetNodeTreeVisible(child.get(), physics, visible);
            }
        }

        // A gun anywhere in a pile makes it a weapon find; otherwise the first stack decides.
        ELootCategory CategoryOf(const std::vector<FLootStack>& contents)
        {
            for (const FLootStack& stack : contents)
            {
                if (CategoryOfItem(stack.itemId) == ELootCategory::Weapon)
                {
                    return ELootCategory::Weapon;
                }
            }
            return contents.empty() ? ELootCategory::Misc : CategoryOfItem(contents.front().itemId);
        }
    }

    void LootSystem::SetVisualCatalog(std::unordered_map<std::string, FLootVisual> catalog, FLootVisual fallback)
    {
        catalog_ = std::move(catalog);
        fallbackVisual_ = fallback;
    }

    std::string LootSystem::DescribeContents(const std::vector<FLootStack>& contents)
    {
        if (contents.empty())
        {
            return {};
        }
        const FLootStack& first = contents.front();
        const FItemDef* definition = FindItemDef(first.itemId);
        std::string name = definition ? std::string(definition->displayName) : first.itemId;
        if (first.count > 1)
        {
            name += fmt::format(" x{}", first.count);
        }
        if (contents.size() > 1)
        {
            name += fmt::format(" (+{} more)", contents.size() - 1);
        }
        return name;
    }

    const FLootVisual& LootSystem::VisualFor(const FLootEntry& entry) const
    {
        if (!entry.contents.empty())
        {
            if (const auto it = catalog_.find(entry.contents.front().itemId); it != catalog_.end() && it->second.valid)
            {
                return it->second;
            }
        }
        return fallbackVisual_;
    }

    void LootSystem::RollContents(FLootEntry& entry, uint32_t roll, bool restock)
    {
        entry.contents.clear();
        if (entry.source == ELootSource::Authored)
        {
            static const auto authored = AuthoredByIndex();
            if (entry.authoredIndex < 0 || entry.authoredIndex >= static_cast<int>(authored.size()))
            {
                return;
            }
            const FAuthoredLoot& def = *authored[entry.authoredIndex].second;
            for (size_t index = 0; index < def.fixed.size(); ++index)
            {
                if (def.fixed[index].empty())
                {
                    continue;
                }
                FLootStack stack{std::string(def.fixed[index]), 1, 0};
                // Fixed guns roll their magazine like any other gun.
                const FItemDef* item = FindItemDef(stack.itemId);
                const FWeaponDef* weapon = item ? FindWeaponDef(item->weaponDefId) : nullptr;
                if (weapon && weapon->magSize > 0)
                {
                    stack.loadedAmmo = static_cast<int>(MixRoll(roll, 41 + static_cast<uint32_t>(index)) %
                                                        static_cast<uint32_t>(weapon->magSize + 1));
                }
                entry.contents.push_back(std::move(stack));
            }
            for (int index = 0; index < def.rolls; ++index)
            {
                entry.contents.push_back(
                    RollLoot(def.rollProfile, MixRoll(roll, 50 + static_cast<uint32_t>(index)), def.rollCategory));
            }
            entry.displayName = std::string(def.displayName);
        }
        else
        {
            ELootCategory only = ELootCategory::Count;
            const uint32_t bias = MixRoll(roll, 60) % 100u;
            if (restock && foodScarce_ && bias < 60u)
            {
                only = ELootCategory::FoodWater;
            }
            else if (restock && medicalScarce_ && bias < 40u)
            {
                only = ELootCategory::Medical;
            }
            FLootStack stack = RollLoot(entry.profile, roll, only);
            if (restock)
            {
                // Over the stock cap for its category: roll again for something else.
                for (uint32_t attempt = 0; attempt < 3; ++attempt)
                {
                    const ELootCategory category = CategoryOfItem(stack.itemId);
                    int available = 0;
                    for (const FLootEntry& other : entries_)
                    {
                        if (other.state == FLootEntry::EState::Available && other.category == category)
                        {
                            ++available;
                        }
                    }
                    const int cap = std::max(1, static_cast<int>(kCategoryStockCap[static_cast<size_t>(category)] *
                                                                 static_cast<float>(entries_.size())));
                    if (available < cap)
                    {
                        break;
                    }
                    stack = RollLoot(entry.profile, MixRoll(roll, 70 + attempt), ELootCategory::FoodWater);
                }
            }
            entry.contents.push_back(std::move(stack));
            entry.displayName = DescribeContents(entry.contents);
        }
        entry.category = CategoryOf(entry.contents);
        director_.SetCategory(entry.directorHandle, entry.category);
    }

    void LootSystem::CreatePoolNodes(Assets::Scene& scene, std::span<const uint32_t> reuse)
    {
        const bool haveVisuals = fallbackVisual_.valid || !catalog_.empty();
        size_t reused = 0;
        for (FLootEntry& entry : entries_)
        {
            if (entry.source == ELootSource::Authored || !haveVisuals)
            {
                continue;
            }
            if (reused < reuse.size())
            {
                entry.nodeInstanceId = reuse[reused++];
                continue;
            }
            const FLootVisual& visual = VisualFor(entry);
            if (!visual.valid)
            {
                continue;
            }
            // Raycast-invisible: pooled loot never stops a bullet or a footstep probe.
            auto node = Assets::SceneBuilder::CreateRenderNode(
                entry.source == ELootSource::Spot ? "nd_loot_spot" : "nd_loot_pile",
                entry.source == ELootSource::Spot ? entry.worldPos : glm::vec3(0.0f, kLootParkedY, 0.0f),
                glm::vec3(1.0f), scene.GenerateInstanceId(), visual.modelId, visual.materials, false,
                glm::angleAxis(entry.yaw, glm::vec3(0.0f, 1.0f, 0.0f)), false);
            auto physics = std::make_shared<Runtime::PhysicsComponent>();
            physics->SetMobility(Runtime::ENodeMobility::Dynamic);
            node->AddComponent(physics);
            scene.AddNode(node);
            entry.nodeInstanceId = node->GetInstanceId();
        }
        scene.MarkDirty();
    }

    void LootSystem::OnSceneLoaded(Assets::Scene& scene, std::span<const FLootSpotSeed> spots)
    {
        Rebuild(scene, spots, {});
    }

    void LootSystem::Rebuild(Assets::Scene& scene, std::span<const FLootSpotSeed> spots,
                             std::span<const uint32_t> reuseNodes)
    {
        entries_.clear();
        zones_.clear();
        hoveredIndex_ = -1;
        ++generation_;
        if (generation_ == 0)
        {
            ++generation_;
        }
        worldSeconds_ = 0.0;
        restockAccumulator_ = 0.0f;
        pileSerial_ = 0;
        director_.Reset(generation_, seed_);
        rollRng_.Reset(seed_ ^ 0x4C4F4F54ULL);

        static const auto authored = AuthoredByIndex();
        for (const auto& node : scene.Nodes())
        {
            if (!node)
            {
                continue;
            }
            const auto it = std::find_if(authored.begin(), authored.end(),
                [&node](const auto& pair) { return pair.first == node->GetName(); });
            if (it == authored.end())
            {
                continue;
            }
            node->RecalcTransform(true);
            FLootEntry entry;
            entry.source = ELootSource::Authored;
            entry.nodeInstanceId = node->GetInstanceId();
            entry.worldPos = node->WorldTranslation();
            entry.authoredIndex = static_cast<int>(std::distance(authored.begin(), it));
            entry.profile = it->second->rollProfile;
            entry.directorHandle = director_.AddSlot(entry.worldPos, entry.profile, ELootCategory::Misc);
            RollContents(entry, director_.Resolve(entry.directorHandle)->roll, false);
            entries_.push_back(std::move(entry));
        }
        const size_t authoredCount = entries_.size();

        for (const FLootSpotSeed& seed : spots)
        {
            FLootEntry entry;
            entry.source = ELootSource::Spot;
            entry.worldPos = seed.position;
            entry.profile = seed.profile;
            entry.directorHandle = director_.AddSlot(entry.worldPos, entry.profile, ELootCategory::Misc);
            const uint32_t roll = director_.Resolve(entry.directorHandle)->roll;
            entry.yaw = static_cast<float>(MixRoll(roll, 90) % 6283u) * 0.001f;
            RollContents(entry, roll, false);
            // Not every drawer has something in it; the empty ones fill in later.
            if (static_cast<float>(MixRoll(roll, 91) % 1000u) >= kInitialStockShare * 1000.0f)
            {
                entry.state = FLootEntry::EState::Cooldown;
                director_.StartCooldown(entry.directorHandle, 0.0, 60.0 + static_cast<double>(MixRoll(roll, 92) % 240u));
            }
            entries_.push_back(std::move(entry));
        }

        for (size_t index = 0; index < kPileCount; ++index)
        {
            FLootEntry entry;
            entry.source = ELootSource::Pile;
            entry.state = FLootEntry::EState::Empty;
            entry.worldPos = glm::vec3(0.0f, kLootParkedY, 0.0f);
            entry.contents.push_back({"rag", 1, 0});   // placeholder visual for pool creation
            entries_.push_back(std::move(entry));
        }

        CreatePoolNodes(scene, reuseNodes);
        for (FLootEntry& entry : entries_)
        {
            if (entry.source == ELootSource::Pile)
            {
                entry.contents.clear();
            }
            if (entry.source == ELootSource::Spot && entry.state != FLootEntry::EState::Available)
            {
                HideEntry(entry, scene, nullptr);
            }
            else if (entry.source == ELootSource::Spot)
            {
                ShowEntry(entry, scene, nullptr);
            }
        }
        BuildZones();
        SPDLOG_INFO("[NextDayz] registered {} loot entries ({} authored, {} spots, {} zones, {} stocked)",
                    entries_.size() - kPileCount, authoredCount, spots.size(), zones_.size(), RemainingCount());
    }

    void LootSystem::BuildZones()
    {
        // Single-linkage clusters: loot within kZoneLinkDistance of each other
        // belongs to the same place.
        std::vector<int> parent(entries_.size());
        std::iota(parent.begin(), parent.end(), 0);
        const auto find = [&parent](int index)
        {
            while (parent[index] != index)
            {
                parent[index] = parent[parent[index]];
                index = parent[index];
            }
            return index;
        };
        for (size_t a = 0; a < entries_.size(); ++a)
        {
            if (entries_[a].source == ELootSource::Pile)
            {
                continue;
            }
            for (size_t b = a + 1; b < entries_.size(); ++b)
            {
                if (entries_[b].source == ELootSource::Pile)
                {
                    continue;
                }
                const glm::vec2 delta(entries_[a].worldPos.x - entries_[b].worldPos.x,
                                      entries_[a].worldPos.z - entries_[b].worldPos.z);
                if (glm::dot(delta, delta) < kZoneLinkDistance * kZoneLinkDistance)
                {
                    parent[find(static_cast<int>(a))] = find(static_cast<int>(b));
                }
            }
        }
        std::unordered_map<int, int> zoneOfRoot;
        for (size_t index = 0; index < entries_.size(); ++index)
        {
            if (entries_[index].source == ELootSource::Pile)
            {
                continue;
            }
            const int root = find(static_cast<int>(index));
            auto [it, inserted] = zoneOfRoot.emplace(root, static_cast<int>(zones_.size()));
            if (inserted)
            {
                zones_.push_back({});
            }
            FLootZone& zone = zones_[it->second];
            zone.center += entries_[index].worldPos;
            ++zone.entries;
            entries_[index].zone = it->second;
        }
        for (FLootZone& zone : zones_)
        {
            zone.center /= static_cast<float>(std::max(zone.entries, 1));
        }
        for (const FLootEntry& entry : entries_)
        {
            if (entry.zone >= 0)
            {
                FLootZone& zone = zones_[entry.zone];
                zone.radius = std::max(zone.radius, glm::distance(glm::vec2(zone.center.x, zone.center.z),
                                                                  glm::vec2(entry.worldPos.x, entry.worldPos.z)));
            }
        }
    }

    void LootSystem::OnSceneUnloaded()
    {
        entries_.clear();
        zones_.clear();
        hoveredIndex_ = -1;
        ++generation_;
        director_.Reset(generation_, seed_);
    }

    void LootSystem::ResetSession(NextEngine& engine)
    {
        Assets::Scene& scene = engine.GetScene();
        std::vector<FLootSpotSeed> spots;
        for (const FLootEntry& entry : entries_)
        {
            if (entry.source == ELootSource::Authored)
            {
                SetNodeTreeVisible(scene.GetNodeByInstanceId(entry.nodeInstanceId), engine.GetPhysicsEngine(), true);
            }
            else if (entry.source == ELootSource::Spot)
            {
                spots.push_back({entry.worldPos, entry.profile});
            }
        }
        // The pooled nodes stay in the scene and are handed back in the same
        // order (spots, then piles) to the rebuilt table.
        std::vector<uint32_t> poolNodes;
        for (const FLootEntry& entry : entries_)
        {
            if (entry.source != ELootSource::Authored && entry.nodeInstanceId != 0)
            {
                poolNodes.push_back(entry.nodeInstanceId);
                if (Assets::Node* node = scene.GetNodeByInstanceId(entry.nodeInstanceId))
                {
                    SetNodeTreeVisible(node, nullptr, false);
                }
            }
        }
        scene.MarkDirty();
        Rebuild(scene, spots, poolNodes);
    }

    void LootSystem::ShowEntry(FLootEntry& entry, Assets::Scene& scene, NextEngine* engine)
    {
        Assets::Node* node = entry.nodeInstanceId ? scene.GetNodeByInstanceId(entry.nodeInstanceId) : nullptr;
        if (!node)
        {
            return;
        }
        if (entry.source == ELootSource::Authored)
        {
            SetNodeTreeVisible(node, engine ? engine->GetPhysicsEngine() : nullptr, true);
        }
        else
        {
            const FLootVisual& visual = VisualFor(entry);
            if (auto render = node->GetComponent<Runtime::RenderComponent>(); render && visual.valid)
            {
                render->SetModelId(visual.modelId);
                render->SetMaterials(visual.materials);
                render->SetVisible(true);
            }
            node->SetTransform(entry.worldPos, glm::angleAxis(entry.yaw, glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(1.0f));
        }
        scene.MarkDirty();
    }

    void LootSystem::HideEntry(FLootEntry& entry, Assets::Scene& scene, NextEngine* engine)
    {
        Assets::Node* node = entry.nodeInstanceId ? scene.GetNodeByInstanceId(entry.nodeInstanceId) : nullptr;
        if (!node)
        {
            return;
        }
        if (entry.source == ELootSource::Authored)
        {
            SetNodeTreeVisible(node, engine ? engine->GetPhysicsEngine() : nullptr, false);
        }
        else if (auto render = node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetVisible(false);
            if (entry.source == ELootSource::Pile)
            {
                node->SetTransform(glm::vec3(0.0f, kLootParkedY, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
            }
        }
        scene.MarkDirty();
    }

    bool LootSystem::NearestLootPos(const glm::vec2& approxXZ, glm::vec3& outWorldPos) const
    {
        float best = std::numeric_limits<float>::max();
        bool found = false;
        for (const FLootEntry& entry : entries_)
        {
            if (entry.source != ELootSource::Authored)
            {
                continue;
            }
            const glm::vec2 xz(entry.worldPos.x, entry.worldPos.z);
            const float d = glm::dot(xz - approxXZ, xz - approxXZ);
            if (d < best)
            {
                best = d;
                outWorldPos = entry.worldPos;
                found = true;
            }
        }
        return found;
    }

    void LootSystem::SetScarcity(bool foodScarce, bool medicalScarce)
    {
        foodScarce_ = foodScarce;
        medicalScarce_ = medicalScarce;
        director_.SetCooldownScale(ELootCategory::FoodWater, foodScarce ? 0.35f : 1.0f);
        director_.SetCooldownScale(ELootCategory::Medical, medicalScarce ? 0.5f : 1.0f);
    }

    void LootSystem::UpdateRestock(const glm::vec3& eyePosition, const glm::vec3& viewForward, NextEngine& engine)
    {
        std::array<int, static_cast<size_t>(ELootCategory::Count)> available{};
        for (const FLootEntry& entry : entries_)
        {
            if (entry.state == FLootEntry::EState::Available)
            {
                ++available[static_cast<size_t>(entry.category)];
            }
        }
        const int total = static_cast<int>(entries_.size());
        for (FLootEntry& entry : entries_)
        {
            if (entry.state != FLootEntry::EState::Cooldown || !entry.directorHandle.IsValid())
            {
                continue;
            }
            const glm::vec3 delta = entry.worldPos - eyePosition;
            const float distance = glm::length(delta);
            const float viewDot = distance > 0.01f ? glm::dot(delta / distance, viewForward) : 1.0f;
            const bool visible = viewDot > config_.AimDotMin && distance < 140.0f;
            // The player has to have left the place, not just this shelf.
            float zoneDistance = distance;
            if (entry.zone >= 0 && entry.zone < static_cast<int>(zones_.size()))
            {
                const FLootZone& zone = zones_[entry.zone];
                zoneDistance = glm::distance(glm::vec2(eyePosition.x, eyePosition.z), glm::vec2(zone.center.x, zone.center.z)) -
                               zone.radius;
            }
            const FLootSlot* slot = director_.Resolve(entry.directorHandle);
            const size_t category = slot ? static_cast<size_t>(slot->category) : static_cast<size_t>(ELootCategory::Misc);
            const int cap = std::max(1, static_cast<int>(kCategoryStockCap[category] * static_cast<float>(total)));
            if (!director_.EvaluateRespawn(entry.directorHandle, worldSeconds_, std::min(distance, zoneDistance), visible,
                                           available[category], cap))
            {
                continue;
            }
            RollContents(entry, director_.Resolve(entry.directorHandle)->roll, true);
            entry.state = FLootEntry::EState::Available;
            ++available[static_cast<size_t>(entry.category)];
            ShowEntry(entry, engine.GetScene(), &engine);
        }
    }

    void LootSystem::Update(float deltaSeconds, const glm::vec3& eyePosition, const glm::vec3& viewForward,
                            NextEngine& engine)
    {
        worldSeconds_ += std::max(deltaSeconds, 0.0f);
        restockAccumulator_ += std::max(deltaSeconds, 0.0f);
        if (restockAccumulator_ >= kRestockInterval)
        {
            restockAccumulator_ = std::fmod(restockAccumulator_, kRestockInterval);
            UpdateRestock(eyePosition, viewForward, engine);
        }

        hoveredIndex_ = -1;
        float bestDot = config_.AimDotMin;
        for (size_t i = 0; i < entries_.size(); ++i)
        {
            const FLootEntry& entry = entries_[i];
            if (entry.state != FLootEntry::EState::Available)
            {
                continue;
            }
            const glm::vec3 delta = entry.worldPos - eyePosition;
            const float distance = glm::length(delta);
            if (distance > config_.ReachMeters || distance < 0.01f)
            {
                continue;
            }
            const float dot = glm::dot(delta / distance, viewForward);
            if (dot > bestDot)
            {
                bestDot = dot;
                hoveredIndex_ = static_cast<int>(i);
            }
        }
    }

    const FLootEntry* LootSystem::Hovered() const
    {
        if (hoveredIndex_ < 0 || hoveredIndex_ >= static_cast<int>(entries_.size()))
        {
            return nullptr;
        }
        return &entries_[hoveredIndex_];
    }

    std::string LootSystem::HoveredPrompt() const
    {
        const FLootEntry* entry = Hovered();
        return entry ? entry->displayName : std::string();
    }

    std::vector<int> LootSystem::Nearby(const glm::vec3& eyePosition, float radius) const
    {
        std::vector<std::pair<float, int>> found;
        for (size_t index = 0; index < entries_.size(); ++index)
        {
            const FLootEntry& entry = entries_[index];
            if (entry.state != FLootEntry::EState::Available)
            {
                continue;
            }
            const float distance = glm::distance(entry.worldPos, eyePosition);
            if (distance <= radius)
            {
                found.emplace_back(distance, static_cast<int>(index));
            }
        }
        std::sort(found.begin(), found.end());
        std::vector<int> result;
        result.reserve(found.size());
        for (const auto& [distance, index] : found)
        {
            result.push_back(index);
        }
        return result;
    }

    std::optional<FLootHandle> LootSystem::ReserveHovered()
    {
        const std::optional<FLootHandle> handle = ReserveEntry(hoveredIndex_);
        if (handle)
        {
            hoveredIndex_ = -1;
        }
        return handle;
    }

    std::optional<FLootHandle> LootSystem::ReserveEntry(int index)
    {
        if (index < 0 || index >= static_cast<int>(entries_.size()))
        {
            return std::nullopt;
        }
        FLootEntry& entry = entries_[index];
        if (entry.state != FLootEntry::EState::Available || entry.contents.empty())
        {
            return std::nullopt;
        }
        if (entry.directorHandle.IsValid() && !director_.Reserve(entry.directorHandle))
        {
            return std::nullopt;
        }
        entry.state = FLootEntry::EState::Reserved;
        return FLootHandle{static_cast<uint32_t>(index), entry.nodeInstanceId, generation_};
    }

    bool LootSystem::IsReservedHandleValid(const FLootHandle& handle) const
    {
        if (!handle.IsValid() || handle.generation != generation_ || handle.index >= entries_.size())
        {
            return false;
        }
        const FLootEntry& entry = entries_[handle.index];
        if (entry.nodeInstanceId != handle.nodeInstanceId || entry.state != FLootEntry::EState::Reserved)
        {
            return false;
        }
        if (!entry.directorHandle.IsValid())
        {
            return true;
        }
        const FLootSlot* slot = director_.Resolve(entry.directorHandle);
        return slot && slot->state == ELootSlotState::Reserved;
    }

    FLootCommitResult LootSystem::Commit(const FLootHandle& handle, Inventory& inventory, NextEngine& engine)
    {
        FLootCommitResult result;
        if (!IsReservedHandleValid(handle))
        {
            return result;
        }
        FLootEntry& entry = entries_[handle.index];
        std::vector<FLootStack> left;
        for (const FLootStack& stack : entry.contents)
        {
            const FItemDef* definition = FindItemDef(stack.itemId);
            const int fit = definition ? inventory.FitCount(stack.itemId, stack.count) : 0;
            if (fit > 0)
            {
                FItemInstanceId instanceId = 0;
                if (inventory.TryAdd(stack.itemId, std::string(definition->displayName), definition->kind, fit,
                                     &instanceId))
                {
                    if (definition->kind == EItemKind::Weapon)
                    {
                        inventory.SetLoadedAmmo(instanceId, stack.loadedAmmo);
                    }
                    result.stacks.push_back({stack.itemId, fit, stack.loadedAmmo});
                }
            }
            if (fit < stack.count)
            {
                left.push_back({stack.itemId, stack.count - fit, stack.loadedAmmo});
            }
        }
        result.taken = !result.stacks.empty();
        result.partial = !left.empty();

        Assets::Scene& scene = engine.GetScene();
        if (!result.taken || result.partial)
        {
            // Nothing (or not everything) fit: what is left stays where it was.
            entry.contents = std::move(left);
            if (entry.source != ELootSource::Authored)
            {
                entry.displayName = DescribeContents(entry.contents);
                ShowEntry(entry, scene, &engine);
            }
            entry.state = FLootEntry::EState::Available;
            if (entry.directorHandle.IsValid())
            {
                director_.Cancel(entry.directorHandle);
            }
            return result;
        }

        entry.contents.clear();
        HideEntry(entry, scene, &engine);
        if (entry.source == ELootSource::Pile)
        {
            entry.state = FLootEntry::EState::Empty;
        }
        else
        {
            director_.Commit(entry.directorHandle, worldSeconds_);
            entry.state = FLootEntry::EState::Cooldown;
        }
        hoveredIndex_ = -1;
        return result;
    }

    bool LootSystem::Cancel(const FLootHandle& handle)
    {
        if (!IsReservedHandleValid(handle))
        {
            return false;
        }
        FLootEntry& entry = entries_[handle.index];
        entry.state = FLootEntry::EState::Available;
        return !entry.directorHandle.IsValid() || director_.Cancel(entry.directorHandle);
    }

    bool LootSystem::SpawnPile(std::vector<FLootStack> contents, const glm::vec3& position, float yaw, NextEngine& engine)
    {
        if (contents.empty())
        {
            return false;
        }
        FLootEntry* target = nullptr;
        double oldest = std::numeric_limits<double>::max();
        for (FLootEntry& entry : entries_)
        {
            if (entry.source != ELootSource::Pile || entry.nodeInstanceId == 0)
            {
                continue;
            }
            if (entry.state == FLootEntry::EState::Empty)
            {
                target = &entry;
                break;
            }
            if (entry.state == FLootEntry::EState::Available && entry.pileTime < oldest)
            {
                oldest = entry.pileTime;
                target = &entry;
            }
        }
        if (!target)
        {
            return false;
        }
        target->contents = std::move(contents);
        target->worldPos = position;
        target->yaw = yaw;
        target->displayName = DescribeContents(target->contents);
        target->category = CategoryOf(target->contents);
        target->state = FLootEntry::EState::Available;
        target->pileTime = worldSeconds_ + static_cast<double>(++pileSerial_) * 1.0e-6;
        ShowEntry(*target, engine.GetScene(), &engine);
        return true;
    }

    std::optional<glm::vec3> LootSystem::PlantCache(const glm::vec3& from, float minDistance, NextEngine& engine)
    {
        FLootEntry* best = nullptr;
        float bestDistance = std::numeric_limits<float>::max();
        for (FLootEntry& entry : entries_)
        {
            if (entry.source != ELootSource::Spot || entry.profile != ELootProfile::Military ||
                entry.state == FLootEntry::EState::Reserved)
            {
                continue;
            }
            const float distance = glm::distance(entry.worldPos, from);
            if (distance >= minDistance && distance < bestDistance)
            {
                bestDistance = distance;
                best = &entry;
            }
        }
        if (!best || !director_.ForceAvailable(best->directorHandle))
        {
            return std::nullopt;
        }
        const uint32_t roll = rollRng_.NextU32();
        const bool sniper = roll % 3u == 0u;
        best->contents = {
            {sniper ? "svd" : "ak", 1, sniper ? 10 : 30},
            {sniper ? "ammo_762" : "ammo_545", sniper ? 20 : 30, 0},
            {"medkit", 1, 0},
        };
        best->displayName = "Supply Cache";
        best->category = ELootCategory::Weapon;
        director_.SetCategory(best->directorHandle, best->category);
        best->state = FLootEntry::EState::Available;
        ShowEntry(*best, engine.GetScene(), &engine);
        return best->worldPos;
    }

    int LootSystem::CategoryCap(ELootCategory category) const
    {
        const size_t index = std::min(static_cast<size_t>(category), kCategoryStockCap.size() - 1);
        return std::max(1, static_cast<int>(kCategoryStockCap[index] * static_cast<float>(entries_.size())));
    }

    bool LootSystem::DebugRestock(int index, NextEngine& engine)
    {
        if (index < 0 || index >= static_cast<int>(entries_.size()))
        {
            return false;
        }
        FLootEntry& entry = entries_[index];
        if (entry.state != FLootEntry::EState::Cooldown || !director_.ForceAvailable(entry.directorHandle))
        {
            return false;
        }
        RollContents(entry, rollRng_.NextU32(), true);
        entry.state = FLootEntry::EState::Available;
        ShowEntry(entry, engine.GetScene(), &engine);
        return true;
    }

    int LootSystem::DebugRestockAll(NextEngine& engine)
    {
        int restocked = 0;
        for (int index = 0; index < static_cast<int>(entries_.size()); ++index)
        {
            restocked += DebugRestock(index, engine) ? 1 : 0;
        }
        return restocked;
    }

    int LootSystem::RemainingCount() const
    {
        return static_cast<int>(std::count_if(entries_.begin(), entries_.end(), [](const FLootEntry& entry)
        {
            return entry.state == FLootEntry::EState::Available || entry.state == FLootEntry::EState::Reserved;
        }));
    }

    int LootSystem::CooldownCount() const
    {
        return static_cast<int>(std::count_if(entries_.begin(), entries_.end(), [](const FLootEntry& entry)
        {
            return entry.state == FLootEntry::EState::Cooldown;
        }));
    }

    int LootSystem::SpotCount() const
    {
        return static_cast<int>(std::count_if(entries_.begin(), entries_.end(),
            [](const FLootEntry& entry) { return entry.source == ELootSource::Spot; }));
    }

    int LootSystem::PileCount() const
    {
        return static_cast<int>(std::count_if(entries_.begin(), entries_.end(), [](const FLootEntry& entry)
        {
            return entry.source == ELootSource::Pile && entry.state == FLootEntry::EState::Available;
        }));
    }
}
