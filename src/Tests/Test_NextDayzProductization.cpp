#include "Engine/Common/CoreMinimal.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>

#include <glm/glm.hpp>

#include "Data/DeterministicRng.hpp"
#include "Debugger/DebugEventLog.hpp"
#include "Debugger/DebugHistory.hpp"
#include "Data/LootTables.hpp"
#include "Weapons/WeaponDefs.hpp"
#include "Inventory/Inventory.hpp"
#include "Player/PlayerActionController.hpp"
#include "Player/SurvivalSystem.hpp"
#include "World/LootDirector.hpp"
#include "Combat/CombatSystem.hpp"
#include "Combat/NoiseSystem.hpp"
#include "Zombies/ZombieSystem.hpp"
#include "World/ZombieSpawnDirector.hpp"
#include "Zombies/ZombieVisualPool.hpp"

TEST_CASE("NextDayz deterministic RNG derives stable streams", "[NextDayz][Productization][Rng]")
{
    NextDayz::FDeterministicRng first(91234);
    NextDayz::FDeterministicRng second(91234);
    NextDayz::FDeterministicRng other(91235);

    for (int index = 0; index < 32; ++index)
    {
        REQUIRE(first.NextU32() == second.NextU32());
    }
    REQUIRE(first.NextU32() != other.NextU32());

    auto lootA = NextDayz::FDeterministicRng(77).Derive(0x4C4F4F54ULL);
    auto lootB = NextDayz::FDeterministicRng(77).Derive(0x4C4F4F54ULL);
    auto zombie = NextDayz::FDeterministicRng(77).Derive(0x5A4F4D42ULL);
    REQUIRE(lootA.NextU32() == lootB.NextU32());
    REQUIRE(lootA.NextU32() != zombie.NextU32());
}

TEST_CASE("NextDayz inventory capacity and batches are atomic", "[NextDayz][Productization][Inventory]")
{
    using namespace NextDayz;
    Inventory inventory;
    REQUIRE(inventory.TotalCapacity() == 10);
    REQUIRE(inventory.TryAdd("medkit", "Medkit", EItemKind::Consumable, 1));
    REQUIRE(inventory.TryAdd("fuel", "Fuel", EItemKind::Misc, 1));
    REQUIRE(inventory.TryAdd("radio", "Radio", EItemKind::Misc, 1));
    REQUIRE(inventory.UsedCapacity() == 9);
    REQUIRE(inventory.TryAdd("bandage", "Bandage", EItemKind::Consumable, 1));
    REQUIRE(inventory.UsedCapacity() == 10);
    REQUIRE_FALSE(inventory.TryAdd("water_bottle", "Water Bottle", EItemKind::Consumable, 1));
    REQUIRE(inventory.CountOf("water_bottle") == 0);

    const std::array<FInventoryAddRequest, 2> rejected = {{
        {"bandage", "Bandage", EItemKind::Consumable, 1},
        {"food_can", "Canned Food", EItemKind::Consumable, 1},
    }};
    const int bandagesBefore = inventory.CountOf("bandage");
    REQUIRE_FALSE(inventory.TryAddBatch(rejected));
    REQUIRE(inventory.CountOf("bandage") == bandagesBefore);
    REQUIRE(inventory.CountOf("food_can") == 0);
}

TEST_CASE("NextDayz stack split conserves quantity", "[NextDayz][Productization][Inventory]")
{
    using namespace NextDayz;
    Inventory inventory;
    FItemInstanceId ammoId = 0;
    REQUIRE(inventory.TryAdd("ammo_545", "5.45x39", EItemKind::Ammo, 30, &ammoId));
    REQUIRE(inventory.CountOf("ammo_545") == 30);
    FItemInstanceId splitId = 0;
    REQUIRE(inventory.TrySplit(ammoId, 10, splitId));
    REQUIRE(splitId != ammoId);
    REQUIRE(inventory.CountOf("ammo_545") == 30);
    REQUIRE(inventory.FindInstance(ammoId)->count == 20);
    REQUIRE(inventory.FindInstance(splitId)->count == 10);
}

TEST_CASE("NextDayz equipped container cannot be removed without transfer space",
          "[NextDayz][Productization][Inventory]")
{
    using namespace NextDayz;
    Inventory inventory;
    FItemInstanceId backpackId = 0;
    REQUIRE(inventory.TryAdd("backpack", "Backpack", EItemKind::Clothing, 1, &backpackId));
    REQUIRE(inventory.TryEquip(backpackId));
    REQUIRE(inventory.TotalCapacity() == 38);
    REQUIRE(inventory.TryAdd("fuel", "Fuel", EItemKind::Misc, 1));
    REQUIRE(inventory.TryAdd("radio", "Radio", EItemKind::Misc, 1));
    REQUIRE(inventory.TryAdd("medkit", "Medkit", EItemKind::Consumable, 1));
    REQUIRE_FALSE(inventory.TryUnequip(backpackId));
    REQUIRE(inventory.IsClothingEquipped("backpack"));
    REQUIRE(inventory.TotalCapacity() == 38);
    REQUIRE(inventory.CountOf("fuel") == 1);
    REQUIRE(inventory.CountOf("medkit") == 1);
}

TEST_CASE("NextDayz weapon instance retains magazine state through moves",
          "[NextDayz][Productization][Inventory]")
{
    using namespace NextDayz;
    Inventory inventory;
    FItemInstanceId pistolId = 0;
    REQUIRE(inventory.TryAdd("pistol", "Makarov", EItemKind::Weapon, 1, &pistolId));
    REQUIRE(inventory.SetLoadedAmmo(pistolId, 7));
    const FContainerId destination = inventory.Containers().back().containerId;
    REQUIRE(inventory.TryMove(pistolId, destination));
    REQUIRE(inventory.LoadedAmmo(pistolId) == 7);
}

TEST_CASE("NextDayz targeted pickup respects container capacity and restrictions", "[NextDayz][Inventory]")
{
    using namespace NextDayz;
    Inventory inventory;
    const FContainerId shirt = inventory.Containers()[0].containerId;
    const FContainerId trousers = inventory.Containers()[1].containerId;
    const FContainerId sling = inventory.Containers()[2].containerId;
    FItemInstanceId bandage = 0;
    REQUIRE(inventory.TryAdd("bandage", "Bandage", EItemKind::Consumable, 3, &bandage, trousers));
    REQUIRE(inventory.FindInstance(bandage)->containerId == trousers);
    REQUIRE(inventory.TryAdd("bandage", "Bandage", EItemKind::Consumable, 1, nullptr, trousers));
    REQUIRE(inventory.FindInstance(bandage)->count == 4);
    REQUIRE(inventory.TryAdd("medkit", "Medkit", EItemKind::Consumable, 1, nullptr, trousers));
    REQUIRE(inventory.ContainerUsed(trousers) == 4);
    REQUIRE(inventory.FitCount("bandage", 1, trousers) == 0);
    REQUIRE_FALSE(inventory.TryAdd("bandage", "Bandage", EItemKind::Consumable, 1, nullptr, trousers));
    REQUIRE_FALSE(inventory.TryAdd("bandage", "Bandage", EItemKind::Consumable, 1, nullptr, sling));
    REQUIRE_FALSE(inventory.TryAdd("bandage", "Bandage", EItemKind::Consumable, 1, nullptr, 12345));
    REQUIRE(inventory.CountOf("bandage") == 4);

    // Moving onto the current full container is a harmless no-op, not a capacity failure.
    REQUIRE(inventory.TryMove(bandage, trousers));
    REQUIRE(inventory.TryMove(bandage, shirt));
    REQUIRE(inventory.FindInstance(bandage)->containerId == shirt);
    REQUIRE(inventory.TryMove(bandage, trousers));
    REQUIRE(inventory.FindInstance(bandage)->containerId == trousers);
}

TEST_CASE("NextDayz ground clothing equips with full pockets and failed swaps roll back", "[NextDayz][Inventory]")
{
    using namespace NextDayz;
    Inventory inventory;
    REQUIRE(inventory.TryAdd("medkit", "Medkit", EItemKind::Consumable, 1));
    REQUIRE(inventory.TryAdd("fuel", "Fuel", EItemKind::Misc, 1));
    REQUIRE(inventory.TryAdd("radio", "Radio", EItemKind::Misc, 1));
    REQUIRE(inventory.TryAdd("bandage", "Bandage", EItemKind::Consumable, 1));
    REQUIRE(inventory.FreeCapacity() == 0);
    REQUIRE_FALSE(inventory.CanAdd("backpack"));
    FItemInstanceId backpack = 0;
    REQUIRE(inventory.TryAdd("backpack", "Backpack", EItemKind::Clothing, 1, &backpack, kWornContainer));
    REQUIRE(inventory.IsWorn(backpack));
    REQUIRE(inventory.TotalCapacity() == 38);
    const FContainerId cargo = inventory.ContainerOwnedBy(backpack)->containerId;
    REQUIRE_FALSE(inventory.TryMove(backpack, cargo));
    // Fill the pack with seven non-weapon 4-volume items (slings cannot hold them).
    REQUIRE(inventory.TryAdd("fuel", "Fuel", EItemKind::Misc, 7, nullptr, cargo));
    REQUIRE(inventory.FreeCapacity() == 0);
    REQUIRE_FALSE(inventory.TryAdd("backpack_small", "Small Backpack", EItemKind::Clothing,
                                    1, nullptr, kWornContainer));
    REQUIRE(inventory.WornInSlot(EEquipSlot::Back) == backpack);
    REQUIRE(inventory.CountOf("backpack_small") == 0);
    REQUIRE(inventory.CountOf("fuel") == 8);
    REQUIRE(inventory.ContainerUsed(cargo) == 28);
    REQUIRE_FALSE(inventory.TryMove(backpack, inventory.Containers()[0].containerId));
    REQUIRE(inventory.IsWorn(backpack));

    // A larger replacement can hold both the old bag and its contents.
    FItemInstanceId replacement = 0;
    REQUIRE(inventory.TryAdd("backpack_military", "Assault Pack", EItemKind::Clothing,
                             1, &replacement, kWornContainer));
    REQUIRE(inventory.IsWorn(replacement));
    REQUIRE_FALSE(inventory.IsWorn(backpack));
    REQUIRE(inventory.ContainerOwnedBy(backpack) == nullptr);
    REQUIRE(inventory.CountOf("fuel") == 8);
    REQUIRE(inventory.TotalCapacity() == 50);
}

TEST_CASE("NextDayz cargo clothing swaps use the space freed by equipping", "[NextDayz][Inventory]")
{
    using namespace NextDayz;
    Inventory inventory;
    FItemInstanceId hoodie = 0;
    REQUIRE(inventory.TryAdd("hoodie", "Hoodie", EItemKind::Clothing, 1, &hoodie, kWornContainer));
    FItemInstanceId jacket = 0;
    const FContainerId shirt = inventory.Containers()[0].containerId;
    const FContainerId trousers = inventory.Containers()[1].containerId;
    REQUIRE(inventory.TryAdd("jacket", "Field Jacket", EItemKind::Clothing, 1, &jacket, shirt));
    REQUIRE(inventory.TryAdd("medkit", "Medkit", EItemKind::Consumable, 1, nullptr, shirt));
    REQUIRE(inventory.TryAdd("fuel", "Fuel", EItemKind::Misc, 1, nullptr, trousers));
    REQUIRE(inventory.TryAdd("fuel", "Fuel", EItemKind::Misc,
                             2, nullptr, inventory.ContainerOwnedBy(hoodie)->containerId));
    REQUIRE(inventory.TryEquip(jacket));
    REQUIRE(inventory.IsWorn(jacket));
    REQUIRE_FALSE(inventory.IsWorn(hoodie));
    REQUIRE(inventory.CountOf("fuel") == 3);
    REQUIRE(inventory.TotalCapacity() == 22);

    // Worn gear can also be dragged straight into a chosen cargo container.
    REQUIRE(inventory.Consume("fuel", 3) == 3);
    REQUIRE(inventory.TryMove(jacket, trousers));
    REQUIRE_FALSE(inventory.IsWorn(jacket));
    REQUIRE(inventory.FindInstance(jacket)->containerId == trousers);
    REQUIRE(inventory.ContainerOwnedBy(jacket) == nullptr);
}

TEST_CASE("NextDayz survival metabolism uses simulation seconds and sprint multipliers",
          "[NextDayz][Productization][Survival]")
{
    using namespace NextDayz;
    FSurvivalConfig tuning;
    tuning.StartingHunger = 100.0f;
    tuning.StartingHydration = 100.0f;
    SurvivalSystem idle;
    SurvivalSystem sprint;
    idle.Configure(tuning);
    sprint.Configure(tuning);
    idle.Reset();
    sprint.Reset();
    idle.Update(60.0f, false);
    sprint.Update(60.0f, true);
    REQUIRE(sprint.Snapshot().hunger < idle.Snapshot().hunger);
    REQUIRE(sprint.Snapshot().hydration < idle.Snapshot().hydration);
    REQUIRE(idle.Snapshot().health == 100.0f);
}

TEST_CASE("NextDayz item use commits once and clamps needs", "[NextDayz][Productization][Survival]")
{
    using namespace NextDayz;
    Inventory inventory;
    FItemInstanceId bottleId = 0;
    REQUIRE(inventory.TryAdd("water_bottle", "Water Bottle", EItemKind::Consumable, 1, &bottleId));
    SurvivalSystem survival;
    survival.Configure(FSurvivalConfig{});
    survival.Reset();
    survival.SetNeeds(90.0f, 90.0f, 75.0f);
    REQUIRE(survival.TryUseItem(inventory, bottleId));
    REQUIRE(survival.Snapshot().hydration == 100.0f);
    REQUIRE(inventory.CountOf("water_bottle") == 0);
    REQUIRE_FALSE(survival.TryUseItem(inventory, bottleId));
}

TEST_CASE("NextDayz use actions cancel before commit and emit exactly one commit",
          "[NextDayz][Productization][Actions]")
{
    using namespace NextDayz;
    PlayerActionController actions;
    actions.Configure(FActionConfig{});
    REQUIRE(actions.BeginUse(EPlayerAction::Drink, 42));
    actions.Update(0.4f);
    actions.RequestCancel();
    actions.Update(0.1f);
    REQUIRE_FALSE(actions.ConsumeItemCommitRequest().has_value());

    REQUIRE(actions.BeginUse(EPlayerAction::Drink, 42));
    actions.Update(1.0f);
    const auto commit = actions.ConsumeItemCommitRequest();
    REQUIRE(commit.has_value());
    REQUIRE(commit->instanceId == 42);
    REQUIRE(commit->action == EPlayerAction::Drink);
    actions.Update(1.0f);
    REQUIRE_FALSE(actions.ConsumeItemCommitRequest().has_value());
}

TEST_CASE("NextDayz damage protection and deprivation death are deterministic",
          "[NextDayz][Productization][Survival]")
{
    using namespace NextDayz;
    SurvivalSystem survival;
    survival.Configure(FSurvivalConfig{});
    survival.Reset();
    REQUIRE(survival.ApplyDamage(10.0f, "infected"));
    REQUIRE_FALSE(survival.ApplyDamage(10.0f, "infected"));
    REQUIRE(survival.Snapshot().health == 90.0f);
    survival.Update(0.36f, false);
    REQUIRE(survival.ApplyDamage(5.0f, "infected"));
    survival.SetNeeds(5.0f, 100.0f, 0.0f);
    survival.Update(3.0f, false);
    REQUIRE(survival.Snapshot().lifeState == EPlayerLifeState::Dead);
    REQUIRE(survival.Snapshot().lastDamageSource == "dehydration");
}

TEST_CASE("NextDayz loot director enforces cooldown visibility distance and generation",
          "[NextDayz][Productization][Loot]")
{
    using namespace NextDayz;
    FLootRespawnTuning tuning;
    tuning.foodWaterSeconds = 10.0;
    tuning.minimumOffscreenSeconds = 5.0;
    LootDirector director;
    director.Configure(tuning);
    director.Reset(4, 991);
    const FLootSlotHandle handle = director.AddSlot({}, ELootProfile::Residential, ELootCategory::FoodWater);
    const uint32_t firstRoll = director.Resolve(handle)->roll;
    REQUIRE(director.Reserve(handle));
    REQUIRE(director.Commit(handle, 0.0));
    REQUIRE_FALSE(director.EvaluateRespawn(handle, 12.0, 20.0f, false, 0, 4));
    REQUIRE_FALSE(director.EvaluateRespawn(handle, 12.0, 80.0f, true, 0, 4));
    REQUIRE_FALSE(director.EvaluateRespawn(handle, 12.0, 80.0f, false, 0, 4));
    REQUIRE_FALSE(director.EvaluateRespawn(handle, 18.0, 80.0f, false, 4, 4));
    REQUIRE(director.EvaluateRespawn(handle, 18.0, 80.0f, false, 3, 4));
    REQUIRE(director.Resolve(handle)->state == ELootSlotState::Available);
    REQUIRE(director.Resolve(handle)->roll != firstRoll);

    director.Reset(5, 991);
    REQUIRE(director.Resolve(handle) == nullptr);
    const FLootSlotHandle replay = director.AddSlot({}, ELootProfile::Residential, ELootCategory::FoodWater);
    REQUIRE(director.Resolve(replay)->roll == firstRoll);
}

TEST_CASE("NextDayz zombies hear investigate chase attack and respect stale handles",
          "[NextDayz][Productization][Zombie]")
{
    using namespace NextDayz;
    ZombieSystem zombies(2);
    const FZombieHandle heard = zombies.Spawn(EZombieProfile::Civilian, glm::vec3(10.0f, 0.0f, 0.0f));
    NoiseSystem noises;
    noises.Emit({}, 25.0f, 1.0f, ENoiseType::Rifle);
    zombies.Update(0.1f, {}, {0.0f, 0.0f, 1.0f}, false, {}, noises.Events());
    REQUIRE(zombies.Resolve(heard)->state == EZombieState::Investigate);

    REQUIRE(zombies.Recycle(heard));
    REQUIRE(zombies.Resolve(heard) == nullptr);
    const FZombieHandle attacker = zombies.Spawn(EZombieProfile::Civilian, {});
    REQUIRE(attacker.generation != heard.generation);
    zombies.Resolve(attacker)->forward = {0.0f, 0.0f, 1.0f};
    const glm::vec3 player(0.0f, 0.0f, 1.0f);
    zombies.Update(0.1f, player, {0.0f, 0.0f, -1.0f}, true,
                   [](const glm::vec3&, const glm::vec3&) { return true; }, {});
    zombies.Update(0.45f, player, {0.0f, 0.0f, -1.0f}, true,
                   [](const glm::vec3&, const glm::vec3&) { return true; }, {});
    const auto damage = zombies.ConsumePlayerDamageRequests();
    REQUIRE(damage.size() == 1);
    REQUIRE(damage.front().source == attacker);
}

TEST_CASE("NextDayz combat resolves hit zones and invalidates recycled proxies",
          "[NextDayz][Productization][Combat]")
{
    using namespace NextDayz;
    ZombieSystem zombies(1);
    CombatSystem combat(zombies);
    const FZombieHandle zombie = zombies.Spawn(EZombieProfile::Civilian, glm::vec3(0.0f, 0.0f, 10.0f));
    combat.HitProxies().Register(77, zombie, EHitZone::Head);
    FWeaponHitEvent hit;
    hit.sequence = 1;
    hit.hitInstanceId = 77;
    hit.origin = {};
    hit.hitPoint = {0.0f, 0.0f, 10.0f};
    hit.baseDamage = 50.0f;
    REQUIRE(combat.ProcessHit(hit, 100.0f));
    REQUIRE(zombies.Resolve(zombie)->state == EZombieState::Dead);
    REQUIRE(zombies.KillCount() == 1);
    REQUIRE(zombies.Recycle(zombie));
    const FZombieHandle reused = zombies.Spawn(EZombieProfile::Civilian, {});
    REQUIRE(reused.generation != zombie.generation);
    REQUIRE_FALSE(combat.ProcessHit(hit, 100.0f));
    REQUIRE(zombies.Resolve(reused)->health == ZombieDef(EZombieProfile::Civilian).maxHealth);
}

TEST_CASE("NextDayz zombie navigation caches paths and replans after target movement",
          "[NextDayz][Productization][Zombie][Navigation]")
{
    using namespace NextDayz;
    ZombieSystem zombies(1);
    const FZombieHandle handle = zombies.Spawn(EZombieProfile::Civilian, {});
    zombies.Resolve(handle)->forward = {0.0f, 0.0f, 1.0f};
    int pathRequests = 0;
    const ZombieSystem::FPathResolver resolvePath = [&pathRequests](const glm::vec3& from, const glm::vec3& to)
    {
        ++pathRequests;
        return std::vector<glm::vec3>{from, to};
    };
    glm::vec3 player(0.0f, 0.0f, 10.0f);
    // Spotted at 10 m it screams first; the chase (and the route) starts after.
    for (int tick = 0; tick < 20 && zombies.Resolve(handle)->state != EZombieState::Chase; ++tick)
    {
        zombies.Update(0.1f, player, {0.0f, 0.0f, -1.0f}, true,
                       [](const glm::vec3&, const glm::vec3&) { return true; }, {}, resolvePath);
    }
    REQUIRE(zombies.Resolve(handle)->state == EZombieState::Chase);
    pathRequests = 0;
    for (int tick = 0; tick < 5; ++tick)
    {
        zombies.Update(0.1f, player, {0.0f, 0.0f, -1.0f}, true,
                       [](const glm::vec3&, const glm::vec3&) { return true; }, {}, resolvePath);
    }
    CHECK(pathRequests == 1);
    CHECK(zombies.Resolve(handle)->position.z > 0.0f);

    player.x += 3.0f;
    zombies.Update(0.1f, player, {0.0f, 0.0f, -1.0f}, true,
                   [](const glm::vec3&, const glm::vec3&) { return true; }, {}, resolvePath);
    CHECK(pathRequests == 2);
}

namespace
{
    bool AlwaysVisible(const glm::vec3&, const glm::vec3&)
    {
        return true;
    }

    NextDayz::FZombieDamage Round(float amount, NextDayz::EHitZone zone, const glm::vec3& direction,
                                  float impact)
    {
        NextDayz::FZombieDamage damage;
        damage.amount = amount;
        damage.zone = zone;
        damage.direction = direction;
        damage.source = -direction * 10.0f;
        damage.impact = impact;
        return damage;
    }
}

TEST_CASE("NextDayz infected steer like bodies: limited turn rate and acceleration",
          "[NextDayz][Productization][Zombie]")
{
    using namespace NextDayz;
    ZombieSystem zombies(1);
    const FZombieHandle handle = zombies.Spawn(EZombieProfile::Civilian, {});
    FZombieRuntime& zombie = *zombies.Resolve(handle);
    zombie.forward = {0.0f, 0.0f, 1.0f};
    zombie.state = EZombieState::Chase;
    zombie.aware = true;
    // Prey directly behind: one 0.1 s tick cannot turn it around.
    const glm::vec3 player(0.0f, 0.0f, -8.0f);
    zombies.Update(0.1f, player, {}, true, AlwaysVisible, {});
    const float turned = glm::degrees(std::acos(glm::clamp(glm::dot(zombie.forward, glm::vec3(0, 0, 1)), -1.0f, 1.0f)));
    CHECK(turned > 5.0f);
    CHECK(turned <= zombies.Config().ChaseTurnDegreesPerSecond * 0.1f + 0.5f);
    CHECK(zombie.Speed() < 1.0f);
    for (int tick = 0; tick < 30; ++tick)
    {
        zombies.Update(0.05f, player, {}, true, AlwaysVisible, {});
    }
    CHECK(glm::dot(zombie.forward, glm::vec3(0, 0, -1)) > 0.95f);
    CHECK(zombie.Speed() > 2.0f);
    CHECK(zombie.Speed() <= ZombieDef(EZombieProfile::Civilian).chaseSpeed * 1.2f);
}

TEST_CASE("NextDayz infected scream before a distant chase but bite at once up close",
          "[NextDayz][Productization][Zombie]")
{
    using namespace NextDayz;
    ZombieSystem zombies(2);
    const FZombieHandle far = zombies.Spawn(EZombieProfile::Civilian, {0.0f, 0.0f, 0.0f});
    zombies.Resolve(far)->forward = {0.0f, 0.0f, 1.0f};
    zombies.Update(0.05f, {0.0f, 0.0f, 12.0f}, {}, true, AlwaysVisible, {});
    REQUIRE(zombies.Resolve(far)->state == EZombieState::Alert);
    CHECK(zombies.AlertedCount() == 1);
    for (int tick = 0; tick < 20; ++tick)
    {
        zombies.Update(0.05f, {0.0f, 0.0f, 12.0f}, {}, true, AlwaysVisible, {});
    }
    CHECK(zombies.Resolve(far)->state == EZombieState::Chase);

    zombies.Reset();
    const FZombieHandle near = zombies.Spawn(EZombieProfile::Civilian, {});
    zombies.Resolve(near)->forward = {0.0f, 0.0f, 1.0f};
    zombies.Update(0.05f, {0.0f, 0.0f, 1.0f}, {}, true, AlwaysVisible, {});
    CHECK(zombies.Resolve(near)->state == EZombieState::Attack);
}

TEST_CASE("NextDayz infected attacks commit on the strike frame and whiff a late sidestep",
          "[NextDayz][Productization][Zombie]")
{
    using namespace NextDayz;
    ZombieSystem zombies(1);
    const FZombieHandle handle = zombies.Spawn(EZombieProfile::Civilian, {});
    FZombieRuntime& zombie = *zombies.Resolve(handle);
    zombie.forward = {0.0f, 0.0f, 1.0f};
    glm::vec3 player(0.0f, 0.0f, 1.0f);
    zombies.Update(0.05f, player, {}, true, AlwaysVisible, {});
    REQUIRE(zombie.state == EZombieState::Attack);
    const float strike = zombies.AttackStrikeTime(zombie.attack);
    // Tracking is locked just before the strike: step out of reach behind it.
    while (zombie.attackTimer < strike - zombies.Config().TrackingLockLeadSeconds + 0.01f)
    {
        zombies.Update(0.02f, player, {}, true, AlwaysVisible, {});
    }
    player = {2.5f, 0.0f, -1.0f};
    for (int tick = 0; tick < 20; ++tick)
    {
        zombies.Update(0.02f, player, {}, true, AlwaysVisible, {});
    }
    CHECK(zombie.attackCommitted);
    CHECK(zombies.ConsumePlayerDamageRequests().empty());
}

TEST_CASE("NextDayz infected flinch under fire, stagger on broken poise, and cannot be stun-locked",
          "[NextDayz][Productization][Zombie][Combat]")
{
    using namespace NextDayz;
    ZombieSystem zombies(1);
    const FZombieHandle handle = zombies.Spawn(EZombieProfile::Military, {});
    FZombieRuntime& zombie = *zombies.Resolve(handle);
    zombie.forward = {0.0f, 0.0f, 1.0f};
    const glm::vec3 fromFront(0.0f, 0.0f, -1.0f);

    // One rifle round: a flinch, a brief slow-down, no stagger.
    FZombieDamageResult result = zombies.ApplyDamage(handle, Round(34.0f, EHitZone::Torso, fromFront, 17.0f));
    CHECK(result.applied);
    CHECK_FALSE(result.staggered);
    CHECK(zombie.flinchSeconds > 0.0f);
    CHECK(zombie.state == EZombieState::Chase);   // being shot wakes it
    CHECK(zombie.aware);

    // A heavy round breaks poise and knocks it back along the shot.
    result = zombies.ApplyDamage(handle, Round(60.0f, EHitZone::Torso, fromFront, 70.0f));
    REQUIRE(result.staggered);
    CHECK(zombie.state == EZombieState::Stagger);
    const float before = zombie.position.z;
    zombies.Update(0.2f, {0.0f, 0.0f, 10.0f}, {}, true, AlwaysVisible, {});
    CHECK(zombie.position.z < before);

    // Immunity: another heavy hit right away does not restart the stagger.
    zombies.Update(0.8f, {0.0f, 0.0f, 10.0f}, {}, true, AlwaysVisible, {});
    REQUIRE(zombie.state != EZombieState::Stagger);
    result = zombies.ApplyDamage(handle, Round(1.0f, EHitZone::Torso, fromFront, 90.0f));
    CHECK_FALSE(result.staggered);
    CHECK(zombies.StaggerCount() == 1);
}

TEST_CASE("NextDayz infected legs cripple and the fall reads the killing hit",
          "[NextDayz][Productization][Zombie][Combat]")
{
    using namespace NextDayz;
    ZombieSystem zombies(3);
    const FZombieHandle legged = zombies.Spawn(EZombieProfile::Military, {});
    zombies.Resolve(legged)->variant.lame = false;
    const glm::vec3 fromFront(0.0f, 0.0f, -1.0f);
    FZombieDamageResult result = zombies.ApplyDamage(legged, Round(40.0f, EHitZone::Leg, fromFront, 10.0f));
    CHECK_FALSE(result.crippled);
    result = zombies.ApplyDamage(legged, Round(40.0f, EHitZone::Leg, fromFront, 10.0f));
    CHECK(result.crippled);
    CHECK(zombies.Resolve(legged)->Lame());

    // Headshot on a standing body: it drops where it stands.
    const FZombieHandle head = zombies.Spawn(EZombieProfile::Civilian, {5.0f, 0.0f, 0.0f});
    zombies.Resolve(head)->forward = {0.0f, 0.0f, 1.0f};
    result = zombies.ApplyDamage(head, Round(34.0f, EHitZone::Head, fromFront, 17.0f));
    CHECK(result.killed);   // x3 on the head: one rifle round
    CHECK(zombies.Resolve(head)->death == EZombieDeath::Crumple);

    // Shot in the chest while charging: momentum pitches it forward and carries it.
    const FZombieHandle runner = zombies.Spawn(EZombieProfile::Civilian, {-5.0f, 0.0f, 0.0f});
    FZombieRuntime& charging = *zombies.Resolve(runner);
    charging.forward = {0.0f, 0.0f, 1.0f};
    charging.velocity = {0.0f, 0.0f, 3.0f};
    result = zombies.ApplyDamage(runner, Round(200.0f, EHitZone::Torso, fromFront, 34.0f));
    REQUIRE(result.killed);
    CHECK(charging.death == EZombieDeath::Forward);
    const float fallStart = charging.position.z;
    zombies.Update(0.3f, {0.0f, 0.0f, 30.0f}, {}, true, AlwaysVisible, {});
    CHECK(charging.position.z > fallStart);
    CHECK(zombies.KillCount() == 2);
}

TEST_CASE("NextDayz infected keep apart and out of the player's capsule",
          "[NextDayz][Productization][Zombie]")
{
    using namespace NextDayz;
    ZombieSystem zombies(2);
    const FZombieHandle a = zombies.Spawn(EZombieProfile::Civilian, {0.0f, 0.0f, 0.0f});
    const FZombieHandle b = zombies.Spawn(EZombieProfile::Civilian, {0.05f, 0.0f, 0.0f});
    for (int tick = 0; tick < 30; ++tick)
    {
        zombies.Update(0.05f, {0.0f, 0.0f, 0.4f}, {}, false, {}, {});
    }
    const glm::vec3 pa = zombies.Resolve(a)->position;
    const glm::vec3 pb = zombies.Resolve(b)->position;
    CHECK(glm::length(glm::vec2(pa.x - pb.x, pa.z - pb.z)) > zombies.Config().SeparationRadius * 0.9f);
    for (const glm::vec3& p : {pa, pb})
    {
        CHECK(glm::length(glm::vec2(p.x, p.z - 0.4f)) > zombies.Config().PlayerPersonalSpace * 0.9f);
    }
}

TEST_CASE("NextDayz hitbox traces pick the nearest capsule and its zone",
          "[NextDayz][Productization][Combat]")
{
    using namespace NextDayz;
    FHitCapsule head{{0.0f, 1.6f, 5.0f}, {0.0f, 1.75f, 5.0f}, 0.12f, EHitZone::Head};
    FHitCapsule torso{{0.0f, 1.0f, 5.0f}, {0.0f, 1.4f, 5.0f}, 0.18f, EHitZone::Torso};
    const glm::vec3 forward(0.0f, 0.0f, 1.0f);
    REQUIRE(RayCapsuleDistance({0.0f, 1.2f, 0.0f}, forward, torso).has_value());
    CHECK(*RayCapsuleDistance({0.0f, 1.2f, 0.0f}, forward, torso) == Catch::Approx(5.0f - 0.18f).margin(1e-3));
    CHECK_FALSE(RayCapsuleDistance({0.5f, 1.2f, 0.0f}, forward, torso).has_value());
    // End caps count, and a ray straight down the axis hits the near cap.
    CHECK(RayCapsuleDistance({0.0f, 1.86f, 0.0f}, forward, head).has_value());
    CHECK(RayCapsuleDistance({0.0f, 3.0f, 5.0f}, {0.0f, -1.0f, 0.0f}, head).has_value());

    ZombieSystem zombies(2);
    CombatSystem combat(zombies);
    const FZombieHandle front = zombies.Spawn(EZombieProfile::Civilian, {0.0f, 0.0f, 5.0f});
    const FZombieHandle behind = zombies.Spawn(EZombieProfile::Civilian, {0.0f, 0.0f, 9.0f});
    const std::array<FHitCapsule, 2> frontBody = {head, torso};
    FHitCapsule farTorso = torso;
    farTorso.a.z = farTorso.b.z = 9.0f;
    combat.SetHitboxes(front, frontBody);
    combat.SetHitboxes(behind, std::span<const FHitCapsule>(&farTorso, 1));

    const std::optional<FHitboxHit> headshot = combat.TraceHitboxes({0.0f, 1.68f, 0.0f}, forward, 100.0f);
    REQUIRE(headshot.has_value());
    CHECK(headshot->zombie == front);
    CHECK(headshot->zone == EHitZone::Head);
    CHECK(headshot->normal.z < -0.9f);
    // A wall at 3 m stops the round before any body.
    CHECK_FALSE(combat.TraceHitboxes({0.0f, 1.2f, 0.0f}, forward, 3.0f).has_value());

    FWeaponHitEvent event;
    event.sequence = 7;
    event.origin = {0.0f, 1.68f, 0.0f};
    event.direction = forward;
    event.hitPoint = headshot->point;
    event.baseDamage = 34.0f;
    event.impactScale = 0.5f;
    event.zombie = headshot->zombie;
    event.zone = headshot->zone;
    REQUIRE(combat.ProcessHit(event, 110.0f));
    const std::vector<FDamageEvent> damage = combat.ConsumeDamageEvents();
    REQUIRE(damage.size() == 1);
    CHECK(damage.front().hitZone == EHitZone::Head);
    CHECK(damage.front().killed);
    CHECK(combat.HeadshotCount() == 1);
    // The dead no longer catch rounds: the next one reaches the body behind.
    const std::optional<FHitboxHit> through = combat.TraceHitboxes({0.0f, 1.2f, 0.0f}, forward, 100.0f);
    REQUIRE(through.has_value());
    CHECK(through->zombie == behind);
}

TEST_CASE("NextDayz spawn director filters safe distance visibility navigation and cap",
          "[NextDayz][Productization][Zombie]")
{
    using namespace NextDayz;
    ZombieSystem zombies(4);
    ZombieSpawnDirector director;
    director.Reset(42);
    director.SetPoints({
        {{20.0f, 0.0f, 0.0f}, EZombieProfile::Civilian},
        {{50.0f, 0.0f, 0.0f}, EZombieProfile::Military},
        {{70.0f, 0.0f, 0.0f}, EZombieProfile::Industrial},
    });
    director.Update(2.0f, {}, zombies,
        [](const glm::vec3& position) { return position.x == 50.0f; },
        [](const glm::vec3& position) { return position.x != 20.0f; });
    REQUIRE(zombies.ActiveCount() == 1);
    REQUIRE(director.SpawnCount() == 1);
    REQUIRE(director.RejectedVisible() == 1);
}

TEST_CASE("NextDayz zombie visual pool is fixed capacity and reusable",
          "[NextDayz][Productization][Zombie]")
{
    using namespace NextDayz;
    ZombieVisualPool pool(1);
    const FZombieHandle first{0, 1};
    const FZombieHandle second{1, 1};
    REQUIRE(pool.Acquire(first) != nullptr);
    REQUIRE(pool.Acquire(second) == nullptr);
    REQUIRE(pool.ActiveCount() == 1);
    REQUIRE(pool.Release(first));
    REQUIRE(pool.Acquire(second) != nullptr);
    REQUIRE(pool.Capacity() == 1);
}

TEST_CASE("NextDayz offers six obtainable clothing choices in every wearable slot", "[NextDayz][Clothing][Loot]")
{
    using namespace NextDayz;
    for (EEquipSlot slot : {EEquipSlot::Head, EEquipSlot::Torso, EEquipSlot::Vest, EEquipSlot::Legs, EEquipSlot::Back})
    {
        size_t choices = 0;
        for (const FItemDef& definition : AllItemDefs())
        {
            if (definition.kind != EItemKind::Clothing || definition.equipSlot != slot) continue;
            INFO(definition.id);
            ++choices;
            bool obtainable = false;
            for (uint8_t index = 0; index < static_cast<uint8_t>(ELootProfile::Count); ++index)
            {
                for (const FLootTableEntry& entry : LootTable(static_cast<ELootProfile>(index)))
                {
                    obtainable |= entry.itemId == definition.id && entry.weight > 0;
                }
            }
            CHECK(obtainable);
        }
        CHECK(choices >= 6);
    }
}

TEST_CASE("NextDayz loot tables roll known items deterministically and respect category filters",
          "[NextDayz][Productization][Loot]")
{
    using namespace NextDayz;
    for (uint8_t index = 0; index < static_cast<uint8_t>(ELootProfile::Count); ++index)
    {
        const ELootProfile profile = static_cast<ELootProfile>(index);
        REQUIRE(ParseLootProfile(LootProfileName(profile)) == profile);
        for (const FLootTableEntry& entry : LootTable(profile))
        {
            INFO(LootProfileName(profile) << " / " << entry.itemId);
            REQUIRE(FindItemDef(entry.itemId) != nullptr);
            REQUIRE(entry.minCount <= entry.maxCount);
        }
        for (uint32_t roll = 0; roll < 64; ++roll)
        {
            const FLootStack a = RollLoot(profile, roll * 7919u);
            const FLootStack b = RollLoot(profile, roll * 7919u);
            REQUIRE(a.itemId == b.itemId);
            REQUIRE(a.count == b.count);
            REQUIRE(a.count >= 1);
        }
    }
    // Restricted rolls stay in the category when the table has one.
    for (uint32_t roll = 0; roll < 64; ++roll)
    {
        REQUIRE(CategoryOfItem(RollLoot(ELootProfile::Military, roll * 31u, ELootCategory::Ammo).itemId) ==
                ELootCategory::Ammo);
        REQUIRE(CategoryOfItem(RollLoot(ELootProfile::Residential, roll * 31u, ELootCategory::FoodWater).itemId) ==
                ELootCategory::FoodWater);
    }
    // Guns come part-loaded, never over a full magazine.
    bool sawPartial = false;
    for (uint32_t roll = 0; roll < 400; ++roll)
    {
        const FLootStack stack = RollLoot(ELootProfile::Military, roll * 104729u, ELootCategory::Weapon);
        const FItemDef* item = FindItemDef(stack.itemId);
        REQUIRE(item != nullptr);
        if (const FWeaponDef* weapon = FindWeaponDef(item->weaponDefId); weapon && weapon->magSize > 0)
        {
            REQUIRE(stack.loadedAmmo >= 0);
            REQUIRE(stack.loadedAmmo <= weapon->magSize);
            sawPartial = sawPartial || (stack.loadedAmmo > 0 && stack.loadedAmmo < weapon->magSize);
        }
    }
    REQUIRE(sawPartial);
}

TEST_CASE("NextDayz worn clothing frees cargo, swaps by slot and drops without its contents",
          "[NextDayz][Productization][Inventory]")
{
    using namespace NextDayz;
    Inventory inventory;
    FItemInstanceId jacketId = 0;
    REQUIRE(inventory.TryAdd("jacket", "Field Jacket", EItemKind::Clothing, 1, &jacketId));
    REQUIRE(inventory.UsedCapacity() == 3);
    REQUIRE(inventory.TryEquip(jacketId));
    REQUIRE(inventory.UsedCapacity() == 0);             // worn items take no cargo space
    REQUIRE(inventory.TotalCapacity() == 22);           // pockets 10 + jacket 12
    REQUIRE(inventory.Warmth() > 0.5f);

    // Wearing a hoodie takes the jacket off into cargo (a swap).
    FItemInstanceId hoodieId = 0;
    REQUIRE(inventory.TryAdd("hoodie", "Hoodie", EItemKind::Clothing, 1, &hoodieId));
    REQUIRE(inventory.TryEquip(hoodieId));
    REQUIRE(inventory.IsWorn(hoodieId));
    REQUIRE_FALSE(inventory.IsWorn(jacketId));
    REQUIRE(inventory.WornInSlot(EEquipSlot::Torso) == hoodieId);

    // Dropping worn armour hands it back; what was in its pouches stays.
    FItemInstanceId vestId = 0;
    REQUIRE(inventory.TryAdd("vest_military", "Military Vest", EItemKind::Clothing, 1, &vestId));
    REQUIRE(inventory.TryEquip(vestId));
    REQUIRE(inventory.Armor() == Catch::Approx(0.3f));
    FItemInstanceId bandageId = 0;
    REQUIRE(inventory.TryAdd("bandage", "Bandage", EItemKind::Consumable, 2, &bandageId));
    REQUIRE(inventory.TryMove(bandageId, inventory.ContainerOwnedBy(vestId)->containerId));
    const std::optional<FItemInstance> dropped = inventory.TryRemove(vestId);
    REQUIRE(dropped.has_value());
    REQUIRE(dropped->defId == "vest_military");
    REQUIRE(inventory.CountOf("bandage") == 2);
    REQUIRE(inventory.Armor() == Catch::Approx(0.0f));

    // Melee weapons ride in the weapon slings like guns, and a blade opens cans.
    FItemInstanceId axeId = 0;
    REQUIRE_FALSE(inventory.CanOpenCans());
    REQUIRE(inventory.TryAdd("axe", "Fire Axe", EItemKind::Melee, 1, &axeId));
    REQUIRE(inventory.FindContainer(inventory.FindInstance(axeId)->containerId)->weaponOnly);
    REQUIRE(inventory.CanOpenCans());
}

TEST_CASE("NextDayz infected wounds bleed until dressed; armour, painkillers and cans behave",
          "[NextDayz][Productization][Survival]")
{
    using namespace NextDayz;
    FSurvivalConfig tuning;
    SurvivalSystem survival;
    survival.Configure(tuning);
    survival.Reset();
    survival.SetNeeds(100.0f, 50.0f, 50.0f);

    // A low roll opens a wound; armour absorbs part of the blow.
    REQUIRE(survival.ApplyInfectedHit(20.0f, 0.3f, 0.0f));
    REQUIRE(survival.Snapshot().health == Catch::Approx(86.0f));
    REQUIRE(survival.Snapshot().bleedingWounds == 1);
    survival.Update(10.0f, false);
    REQUIRE(survival.Snapshot().health < 86.0f);
    REQUIRE(survival.Snapshot().lastDamageSource == "blood loss");

    Inventory inventory;
    FItemInstanceId ragId = 0;
    REQUIRE(inventory.TryAdd("rag", "Rags", EItemKind::Consumable, 1, &ragId));
    std::string message;
    REQUIRE(survival.TryUseItem(inventory, ragId, &message));
    REQUIRE(survival.Snapshot().bleedingWounds == 0);
    REQUIRE_FALSE(message.empty());

    // Painkillers heal over their duration, not at once.
    survival.SetNeeds(50.0f, 50.0f, 50.0f);
    FItemInstanceId pillsId = 0;
    REQUIRE(inventory.TryAdd("painkillers", "Painkillers", EItemKind::Consumable, 1, &pillsId));
    REQUIRE(survival.TryUseItem(inventory, pillsId));
    REQUIRE(survival.Snapshot().health == Catch::Approx(50.0f));
    survival.Update(tuning.PainkillerSeconds, false);
    REQUIRE(survival.Snapshot().health == Catch::Approx(50.0f + tuning.PainkillerHeal).margin(0.5f));

    // A can without an opener spills; with one it does not.
    survival.SetNeeds(100.0f, 20.0f, 100.0f);
    FItemInstanceId canId = 0;
    REQUIRE(inventory.TryAdd("food_can", "Canned Beans", EItemKind::Consumable, 2, &canId));
    REQUIRE(survival.TryUseItem(inventory, canId, &message));
    REQUIRE(survival.Snapshot().hunger == Catch::Approx(20.0f + 32.0f * tuning.UnopenedCanShare));
    REQUIRE(inventory.TryAdd("can_opener", "Can Opener", EItemKind::Misc, 1));
    survival.SetNeeds(100.0f, 20.0f, 100.0f);
    REQUIRE(survival.TryUseItem(inventory, inventory.FindFirst("food_can")->instanceId));
    REQUIRE(survival.Snapshot().hunger == Catch::Approx(52.0f));

    // Drinking leaves the bottle to refill.
    FItemInstanceId bottleId = 0;
    REQUIRE(inventory.TryAdd("water_bottle", "Water Bottle", EItemKind::Consumable, 1, &bottleId));
    REQUIRE(survival.TryUseItem(inventory, bottleId));
    REQUIRE(inventory.CountOf("water_bottle_empty") == 1);

    // Starved: too weak to sprint. Resting needs closed wounds.
    survival.SetNeeds(60.0f, 5.0f, 50.0f);
    REQUIRE(survival.IsWeak());
    survival.SetBleeding(1);
    REQUIRE_FALSE(survival.Rest());
    survival.SetBleeding(0);
    REQUIRE(survival.Rest());
}

TEST_CASE("NextDayz cold burns food faster", "[NextDayz][Productization][Survival]")
{
    using namespace NextDayz;
    FSurvivalConfig tuning;
    tuning.StartingHunger = 100.0f;
    SurvivalSystem warm;
    SurvivalSystem cold;
    warm.Configure(tuning);
    cold.Configure(tuning);
    warm.Reset();
    cold.Reset();
    cold.SetCold(1.0f);
    warm.Update(120.0f, false);
    cold.Update(120.0f, false);
    REQUIRE(cold.Snapshot().hunger < warm.Snapshot().hunger);
}

TEST_CASE("NextDayz loot director shortens a scarce category cooldown", "[NextDayz][Productization][Loot]")
{
    using namespace NextDayz;
    FLootRespawnTuning tuning;
    tuning.foodWaterSeconds = 100.0;
    tuning.minimumOffscreenSeconds = 1.0;
    LootDirector director;
    director.Configure(tuning);
    director.Reset(1, 5);
    const FLootSlotHandle handle = director.AddSlot({}, ELootProfile::Residential, ELootCategory::FoodWater);
    REQUIRE(director.Reserve(handle));
    REQUIRE(director.Commit(handle, 0.0));
    REQUIRE_FALSE(director.EvaluateRespawn(handle, 40.0, 200.0f, false, 0, 10));
    director.SetCooldownScale(ELootCategory::FoodWater, 0.35f);
    REQUIRE(director.EvaluateRespawn(handle, 41.5, 200.0f, false, 0, 10));
}

TEST_CASE("NextDayz spawn director brings groups and respects the cap", "[NextDayz][Productization][Zombie]")
{
    using namespace NextDayz;
    ZombieSystem zombies(8);
    ZombieSpawnDirector director;
    director.Reset(7);
    director.SetGlobalCap(4);
    FZombieSpawnPoint horde{{60.0f, 0.0f, 0.0f}, EZombieProfile::Civilian};
    horde.groupSize = 3;
    FZombieSpawnPoint pair{{0.0f, 0.0f, 70.0f}, EZombieProfile::Military};
    pair.groupSize = 2;
    director.SetPoints({horde, pair});
    director.Update(1.0f, {}, zombies, {}, {});
    REQUIRE(zombies.ActiveCount() == 4);
    REQUIRE(director.SpawnCount() == 4);
}

TEST_CASE("NextDayz spawn director records why each point did or did not spawn", "[NextDayz][Productization][Debug]")
{
    using namespace NextDayz;
    ZombieSystem zombies(8);
    ZombieSpawnDirector director;
    director.Reset(42);
    director.SetPoints({
        {{10.0f, 0.0f, 0.0f}, EZombieProfile::Civilian},     // inside the safe distance
        {{300.0f, 0.0f, 0.0f}, EZombieProfile::Civilian},    // beyond activation
        {{50.0f, 0.0f, 0.0f}, EZombieProfile::Military},     // in view
        {{70.0f, 0.0f, 0.0f}, EZombieProfile::Industrial},   // off the nav grid
        {{0.0f, 0.0f, 90.0f}, EZombieProfile::Wilderness},   // fine
    });
    const auto visible = [](const glm::vec3& position) { return position.x == 50.0f; };
    const auto navigable = [](const glm::vec3& position) { return position.x != 70.0f; };
    director.Update(2.0f, {}, zombies, visible, navigable);
    const auto& points = director.Points();
    CHECK(points[0].verdict == ESpawnVerdict::TooClose);
    CHECK(points[1].verdict == ESpawnVerdict::TooFar);
    CHECK(points[2].verdict == ESpawnVerdict::Visible);
    CHECK(points[3].verdict == ESpawnVerdict::NotNavigable);
    REQUIRE(points[4].verdict == ESpawnVerdict::Spawned);
    CHECK(points[4].playerDistance == Catch::Approx(90.0f));

    director.Update(1.0f, {}, zombies, visible, navigable);
    CHECK(points[4].verdict == ESpawnVerdict::Populated);

    // A full cap stops the pass before any distance check.
    director.SetGlobalCap(1);
    director.Update(1.0f, {}, zombies, visible, navigable);
    CHECK(points[0].verdict == ESpawnVerdict::CapReached);

    // The group is gone: the point cools down.
    REQUIRE(zombies.Recycle(points[4].active[0]));
    director.Update(1.0f, {}, zombies, visible, navigable);
    CHECK(points[4].verdict == ESpawnVerdict::Cooldown);
    CHECK(std::string_view(SpawnVerdictName(points[4].verdict)) == "Cooldown");
}

TEST_CASE("NextDayz loot director records what holds a restock back", "[NextDayz][Productization][Debug]")
{
    using namespace NextDayz;
    FLootRespawnTuning tuning;
    tuning.foodWaterSeconds = 10.0;
    tuning.minimumOffscreenSeconds = 5.0;
    LootDirector director;
    director.Configure(tuning);
    director.Reset(1, 77);
    const FLootSlotHandle handle = director.AddSlot({}, ELootProfile::Residential, ELootCategory::FoodWater);
    REQUIRE(director.Reserve(handle));
    REQUIRE(director.Commit(handle, 0.0));
    CHECK(director.ReadyAt(handle) == Catch::Approx(10.0));
    director.SetCooldownScale(ELootCategory::FoodWater, 0.5f);
    CHECK(director.ReadyAt(handle) == Catch::Approx(5.0));
    director.SetCooldownScale(ELootCategory::FoodWater, 1.0f);

    const auto block = [&] { return director.Resolve(handle)->block; };
    REQUIRE_FALSE(director.EvaluateRespawn(handle, 12.0, 80.0f, true, 0, 4));
    CHECK(block() == ELootRespawnBlock::Watched);
    REQUIRE_FALSE(director.EvaluateRespawn(handle, 12.0, 20.0f, false, 0, 4));
    CHECK(block() == ELootRespawnBlock::PlayerNear);
    REQUIRE_FALSE(director.EvaluateRespawn(handle, 6.0, 80.0f, false, 0, 4));
    CHECK(block() == ELootRespawnBlock::Cooling);
    REQUIRE_FALSE(director.EvaluateRespawn(handle, 12.0, 20.0f, false, 0, 4));
    REQUIRE_FALSE(director.EvaluateRespawn(handle, 12.0, 80.0f, false, 0, 4));
    CHECK(block() == ELootRespawnBlock::Offscreen);
    REQUIRE_FALSE(director.EvaluateRespawn(handle, 18.0, 80.0f, false, 4, 4));
    CHECK(block() == ELootRespawnBlock::CategoryFull);
    REQUIRE(director.EvaluateRespawn(handle, 18.0, 80.0f, false, 3, 4));
    CHECK(block() == ELootRespawnBlock::None);
    CHECK(director.ReadyAt(handle) == Catch::Approx(0.0));
}

TEST_CASE("NextDayz infected report perception and route budget for the debugger", "[NextDayz][Productization][Debug]")
{
    using namespace NextDayz;
    FZombieConfig config;
    config.MaxPathSolvesPerUpdate = 1;
    ZombieSystem zombies(2);
    zombies.Configure(config);
    const FZombieHandle first = zombies.Spawn(EZombieProfile::Civilian, {0.0f, 0.0f, 0.0f});
    const FZombieHandle second = zombies.Spawn(EZombieProfile::Civilian, {1.0f, 0.0f, 0.0f});
    zombies.Resolve(first)->forward = {0.0f, 0.0f, 1.0f};
    zombies.Resolve(second)->forward = {0.0f, 0.0f, 1.0f};
    const ZombieSystem::FPathResolver route = [](const glm::vec3& from, const glm::vec3& to)
    {
        return std::vector<glm::vec3>{from, to};
    };
    const glm::vec3 player(0.0f, 0.0f, 10.0f);
    const auto clear = [](const glm::vec3&, const glm::vec3&) { return true; };

    zombies.Update(0.1f, player, {0.0f, 0.0f, -1.0f}, true, clear, {}, route);
    CHECK(zombies.Resolve(first)->seesPlayer);
    for (int tick = 0; tick < 20 && (zombies.Resolve(first)->state != EZombieState::Chase ||
                                     zombies.Resolve(second)->state != EZombieState::Chase); ++tick)
    {
        zombies.Update(0.1f, player, {0.0f, 0.0f, -1.0f}, true, clear, {}, route);
    }
    REQUIRE(zombies.Resolve(first)->state == EZombieState::Chase);
    REQUIRE(zombies.Resolve(second)->state == EZombieState::Chase);
    // Both want a route on their first chase update; the budget plans one.
    zombies.Update(0.1f, player, {0.0f, 0.0f, -1.0f}, true, clear, {}, route);
    CHECK(zombies.PathSolvesLastUpdate() == 1);
    CHECK(zombies.PathDeferredLastUpdate() == 1);
    zombies.Update(0.1f, player, {0.0f, 0.0f, -1.0f}, true, clear, {}, route);
    CHECK(zombies.PathSolvesLastUpdate() == 1);
    CHECK(zombies.PathDeferredLastUpdate() == 0);
    CHECK(zombies.Resolve(first)->pathSolves == 1);
    CHECK(zombies.Resolve(second)->pathSolves == 1);
    CHECK_FALSE(zombies.Resolve(first)->pathFailed);

    // Out of sight it no longer sees; a fresh noise in range is heard once.
    const auto blocked = [](const glm::vec3&, const glm::vec3&) { return false; };
    FNoiseEvent shot;
    shot.sequence = 7;
    shot.position = {0.0f, 0.0f, 5.0f};
    shot.radius = 20.0f;
    shot.strength = 1.0f;
    shot.type = ENoiseType::Rifle;
    shot.remainingSeconds = 1.0f;
    const std::array<FNoiseEvent, 1> noise = {shot};
    zombies.Update(0.1f, player, {0.0f, 0.0f, -1.0f}, true, blocked, noise, route);
    CHECK_FALSE(zombies.Resolve(first)->seesPlayer);
    CHECK(zombies.Resolve(first)->heardNoise > 0.0f);
    zombies.Update(0.1f, player, {0.0f, 0.0f, -1.0f}, true, blocked, noise, route);
    CHECK(zombies.Resolve(first)->heardNoise == 0.0f);
}

TEST_CASE("NextDayz debug event log folds repeats and keeps the newest", "[NextDayz][Productization][Debug]")
{
    using namespace NextDayz::Debug;
    EventLog log(3);
    log.SetClock(1, 0.5);
    log.Push(EChannel::Weapon, 0, "Fired ak");
    log.Push(EChannel::Weapon, 0, "Fired ak");
    REQUIRE(log.Size() == 1);
    CHECK(log.At(0).repeats == 2);
    CHECK(log.TotalPushed() == 1);
    CHECK(log.ChannelCount(EChannel::Weapon) == 2);

    log.Push(EChannel::Infected, 1, "a");
    log.Push(EChannel::Loot, 0, "b");
    log.SetClock(9, 4.0);
    log.Push(EChannel::Noise, 0, "c");
    REQUIRE(log.Size() == 3);
    CHECK(log.At(0).text == "a");
    CHECK(log.Newest()->text == "c");
    CHECK(log.Newest()->frame == 9);
    CHECK(log.TotalPushed() == 4);

    const glm::vec3 where(1.0f, 2.0f, 3.0f);
    log.Push(EChannel::Noise, 2, "c", &where);
    CHECK(log.Size() == 3);
    CHECK(log.Newest()->repeats == 2);
    CHECK(log.Newest()->severity == 2);
    CHECK(log.Newest()->hasPosition);

    log.Clear();
    CHECK(log.Size() == 0);
    CHECK(log.ChannelCount(EChannel::Noise) == 0);
    CHECK(log.TotalPushed() == 4);   // serials stay unique across a clear

    TSeries<4> series;
    for (int value = 1; value <= 5; ++value)
    {
        series.Push(static_cast<float>(value));
    }
    CHECK(series.Count() == 4);
    CHECK(series.At(0) == 2.0f);
    CHECK(series.Latest() == 5.0f);
    CHECK(series.Back(2) == 3.0f);
    CHECK(series.Min() == 2.0f);
    CHECK(series.Max() == 5.0f);
    CHECK(series.PlotValues()[series.PlotOffset()] == 2.0f);
}
