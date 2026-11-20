#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/Net/NetProtocol.hpp"

#include <cstddef>
#include <cstdio>

namespace
{
    using namespace NextDayz;

    template<class T>
    void PrintLayout(const char* name, std::initializer_list<size_t> offsets)
    {
        std::printf("%s %zu %zu", name, sizeof(T), alignof(T));
        for (size_t offset : offsets)
        {
            std::printf(" %zu", offset);
        }
        std::puts("");
    }

    template<class T>
    void ClearWireBytes(T& value)
    {
        // Production encodes padding as well as fields. Give the probe a defined
        // padding pattern so the ABI comparison is independent of stack contents.
        static_assert(std::is_trivially_copyable_v<T>);
        std::memset(&value, 0, sizeof(value));
    }

    template<class T>
    bool CheckMessage(const char* name, Net::EMessage type, const T& input)
    {
        NextNet::FNetWriter writer;
        Net::Write(writer, input);
        NextNet::FNetReader reader(writer.Bytes());
        Net::EMessage actual{};
        T decoded{};
        if (!Net::ReadHeader(reader, actual) || actual != type ||
            !Net::Read(reader, decoded) || !reader.AtEnd())
        {
            return false;
        }

        std::printf("%s %zu ", name, writer.Size());
        for (const std::byte value : writer.Bytes())
        {
            std::printf("%02x", std::to_integer<unsigned>(value));
        }
        std::puts("");
        return true;
    }
}

int main()
{
    using namespace NextDayz;
    using TimeState = TimeSystem::FState;
    PrintLayout<FSurvivorPresence>("FSurvivorPresence", {
        offsetof(FSurvivorPresence, id), offsetof(FSurvivorPresence, feet), offsetof(FSurvivorPresence, eye),
        offsetof(FSurvivorPresence, forward), offsetof(FSurvivorPresence, alive), offsetof(FSurvivorPresence, sightScale)});
    PrintLayout<FPlayerLocomotionState>("FPlayerLocomotionState", {
        offsetof(FPlayerLocomotionState, desiredStance), offsetof(FPlayerLocomotionState, actualStance),
        offsetof(FPlayerLocomotionState, gait), offsetof(FPlayerLocomotionState, localMove),
        offsetof(FPlayerLocomotionState, worldVelocity), offsetof(FPlayerLocomotionState, horizontalSpeed),
        offsetof(FPlayerLocomotionState, jumpPhase), offsetof(FPlayerLocomotionState, jumpPhaseTime01),
        offsetof(FPlayerLocomotionState, traversalAction), offsetof(FPlayerLocomotionState, traversalTime01),
        offsetof(FPlayerLocomotionState, traversalHeight), offsetof(FPlayerLocomotionState, traversalYaw),
        offsetof(FPlayerLocomotionState, traversalHand), offsetof(FPlayerLocomotionState, traversalHandWeight),
        offsetof(FPlayerLocomotionState, onGround), offsetof(FPlayerLocomotionState, standBlocked)});
    PrintLayout<TimeState>("TimeSystem::FState", {
        offsetof(TimeState, minutes), offsetof(TimeState, nextWeatherChange), offsetof(TimeState, weatherRng),
        offsetof(TimeState, weather), offsetof(TimeState, blend)});
    PrintLayout<FDamageEvent>("FDamageEvent", {
        offsetof(FDamageEvent, sequence), offsetof(FDamageEvent, target), offsetof(FDamageEvent, hitZone),
        offsetof(FDamageEvent, amount), offsetof(FDamageEvent, impact), offsetof(FDamageEvent, worldPoint),
        offsetof(FDamageEvent, direction), offsetof(FDamageEvent, killed), offsetof(FDamageEvent, staggered),
        offsetof(FDamageEvent, crippled)});
    PrintLayout<FWeaponAttachments>("FWeaponAttachments", {});
    PrintLayout<FGridPlacement>("FGridPlacement", {
        offsetof(FGridPlacement, x), offsetof(FGridPlacement, y), offsetof(FGridPlacement, rotated)});

    Net::FHello hello{};
    hello.name = "W0-Ada";
    hello.scene = "training_ground";
    hello.ticket = "fixture";

    Net::FSnapshot snapshot{};
    ClearWireBytes(snapshot.time);
    snapshot.time.minutes = 515.5;
    snapshot.time.nextWeatherChange = 621.25;
    snapshot.time.weatherRng = 0x0102030405060708ull;
    snapshot.time.blend = 0.625f;
    snapshot.survivors.resize(1);
    auto& player = snapshot.survivors.front();
    ClearWireBytes(player.presence);
    player.presence.id = 42;
    player.presence.feet = {9.25f, -1.5f, 5.0f};
    player.presence.eye = {9.0f, 0.5f, 5.0f};
    player.presence.forward = {0.0f, 0.0f, 1.0f};
    player.presence.sightScale = 0.5f;
    player.presence.alive = true;
    auto& locomotion = player.presentation.locomotion;
    ClearWireBytes(locomotion);
    locomotion.localMove = {-0.5f, 1.0f};
    locomotion.worldVelocity = {2.0f, -1.0f, 3.25f};
    locomotion.traversalHand[1] = {12.0f, 5.0f, 8.0f};
    locomotion.traversalHandWeight[1] = 0.75f;
    locomotion.onGround = true;
    player.presentation.weaponId = "ak";
    player.presentation.hasWeapon = true;
    player.presentation.slotAttachments[0][0] = EWeaponAttachment::RedDot;
    player.clothing.push_back("jacket_field");

    Net::FDamageNotice notice{};
    notice.shooterId = 42;
    ClearWireBytes(notice.damage);
    notice.damage.sequence = 28;
    notice.damage.target = {8, 1};
    notice.damage.hitZone = EHitZone::Leg;
    notice.damage.amount = 15.5f;
    notice.damage.worldPoint = {1.0f, 2.0f, 3.0f};
    notice.damage.direction = {-1.0f, 0.0f, 0.0f};
    notice.damage.crippled = true;

    Inventory inventory;
    const auto* item = FindItemDef("bandage");
    if (!item || !inventory.TryAdd(std::string(item->id), std::string(item->displayName), item->kind, 2))
    {
        return 1;
    }
    Net::FInventorySync inventorySync{.life = 3, .inventory = inventory.Capture()};
    for (auto& instance : inventorySync.inventory.instances)
    {
        const auto placement = instance.placement;
        ClearWireBytes(instance.placement);
        instance.placement.x = placement.x;
        instance.placement.y = placement.y;
        instance.placement.rotated = placement.rotated;
    }

    return CheckMessage("Hello", Net::EMessage::Hello, hello) &&
           CheckMessage("PlayerState", Net::EMessage::PlayerState, player) &&
           CheckMessage("Snapshot", Net::EMessage::Snapshot, snapshot) &&
           CheckMessage("DamageNotice", Net::EMessage::DamageNotice, notice) &&
           CheckMessage("InventorySync", Net::EMessage::InventorySync, inventorySync) ? 0 : 1;
}
