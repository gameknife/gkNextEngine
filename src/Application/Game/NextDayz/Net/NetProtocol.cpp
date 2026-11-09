#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/Net/NetProtocol.hpp"

#if GK_MODULE_NEXTNET

namespace NextDayz::Net
{
    using NextNet::FNetReader;
    using NextNet::FNetWriter;

    namespace
    {
        void Header(FNetWriter& writer, EMessage message) { writer.Write(message); }

        void WritePresentation(FNetWriter& writer, const FPlayerPresentationState& p)
        {
            writer.Write(p.locomotion)
                .Write(p.action)
                .Write(p.actionTime01)
                .Write(p.aimWeight)
                .Write(p.aimPitchRadians)
                .Write(p.aimYawRadians)
                .Write(p.aimDirection)
                .Write(p.sprintAlpha)
                .Write<uint8_t>(p.hasWeapon ? 1 : 0)
                .WriteString(p.weaponId)
                .Write(p.weaponAction)
                .Write(p.weaponActionTime01)
                .Write<int8_t>(static_cast<int8_t>(p.activeWeaponSlot))
                .Write<int8_t>(static_cast<int8_t>(p.switchSourceSlot))
                .Write<int8_t>(static_cast<int8_t>(p.switchTargetSlot));
            for (const std::string& id : p.slotWeaponIds) writer.WriteString(id);
            for (const FWeaponAttachments& attachments : p.slotAttachments) writer.Write(attachments);
        }

        void ReadPresentation(FNetReader& reader, FPlayerPresentationState& p)
        {
            reader.Read(p.locomotion);
            reader.Read(p.action);
            reader.Read(p.actionTime01);
            reader.Read(p.aimWeight);
            reader.Read(p.aimPitchRadians);
            reader.Read(p.aimYawRadians);
            reader.Read(p.aimDirection);
            reader.Read(p.sprintAlpha);
            p.hasWeapon = reader.Read<uint8_t>() != 0;
            p.weaponId = reader.ReadString();
            reader.Read(p.weaponAction);
            reader.Read(p.weaponActionTime01);
            p.activeWeaponSlot = reader.Read<int8_t>();
            p.switchSourceSlot = reader.Read<int8_t>();
            p.switchTargetSlot = reader.Read<int8_t>();
            for (std::string& id : p.slotWeaponIds) id = reader.ReadString();
            for (FWeaponAttachments& attachments : p.slotAttachments) reader.Read(attachments);
        }

        void WriteClothing(FNetWriter& writer, const std::vector<std::string>& clothing)
        {
            writer.Write(static_cast<uint8_t>(std::min<size_t>(clothing.size(), 16)));
            for (size_t index = 0; index < clothing.size() && index < 16; ++index) writer.WriteString(clothing[index]);
        }

        bool ReadClothing(FNetReader& reader, std::vector<std::string>& clothing)
        {
            const auto count = reader.Read<uint8_t>();
            if (reader.Failed() || count > 16) return false;
            clothing.resize(count);
            for (std::string& item : clothing) item = reader.ReadString();
            return !reader.Failed();
        }

        // Bounded so a malformed packet cannot ask for a huge allocation.
        constexpr uint16_t kMaxStacks = 512;

        void WriteStacks(FNetWriter& writer, const std::vector<FLootStack>& stacks)
        {
            writer.Write(static_cast<uint16_t>(std::min<size_t>(stacks.size(), kMaxStacks)));
            for (size_t index = 0; index < stacks.size() && index < kMaxStacks; ++index)
            {
                const FLootStack& stack = stacks[index];
                writer.WriteString(stack.itemId)
                    .Write<int32_t>(stack.count)
                    .Write<int32_t>(stack.loadedAmmo)
                    .Write(stack.attachments);
            }
        }

        bool ReadStacks(FNetReader& reader, std::vector<FLootStack>& stacks)
        {
            const auto count = reader.Read<uint16_t>();
            if (reader.Failed() || count > kMaxStacks) return false;
            stacks.resize(count);
            for (FLootStack& stack : stacks)
            {
                stack.itemId = reader.ReadString();
                stack.count = reader.Read<int32_t>();
                stack.loadedAmmo = reader.Read<int32_t>();
                reader.Read(stack.attachments);
            }
            return !reader.Failed();
        }

        constexpr uint16_t kMaxInventoryEntries = 1024;

        void WriteInventory(FNetWriter& writer, const FInventoryState& state)
        {
            writer.Write(static_cast<uint16_t>(std::min<size_t>(state.instances.size(), kMaxInventoryEntries)));
            for (size_t index = 0; index < state.instances.size() && index < kMaxInventoryEntries; ++index)
            {
                const FItemInstance& item = state.instances[index];
                writer.Write(item.instanceId)
                    .WriteString(item.defId)
                    .Write<int32_t>(item.count)
                    .Write(item.containerId)
                    .Write<int32_t>(item.loadedAmmo)
                    .Write(item.attachments)
                    .Write(item.placement);
            }
            writer.Write(static_cast<uint16_t>(std::min<size_t>(state.containers.size(), kMaxInventoryEntries)));
            for (size_t index = 0; index < state.containers.size() && index < kMaxInventoryEntries; ++index)
            {
                const FInventoryContainer& container = state.containers[index];
                writer.Write(container.containerId)
                    .Write(container.ownerItemInstanceId)
                    .WriteString(container.displayName)
                    .Write(container.size)
                    .Write<uint8_t>(static_cast<uint8_t>((container.active ? 1 : 0) | (container.weaponOnly ? 2 : 0)));
            }
            writer.Write(state.nextInstanceId).Write(state.nextContainerId);
        }

        bool ReadInventory(FNetReader& reader, FInventoryState& state)
        {
            const auto items = reader.Read<uint16_t>();
            if (reader.Failed() || items > kMaxInventoryEntries) return false;
            state.instances.resize(items);
            for (FItemInstance& item : state.instances)
            {
                reader.Read(item.instanceId);
                item.defId = reader.ReadString();
                item.count = reader.Read<int32_t>();
                reader.Read(item.containerId);
                item.loadedAmmo = reader.Read<int32_t>();
                reader.Read(item.attachments);
                reader.Read(item.placement);
            }
            const auto containers = reader.Read<uint16_t>();
            if (reader.Failed() || containers > kMaxInventoryEntries) return false;
            state.containers.resize(containers);
            for (FInventoryContainer& container : state.containers)
            {
                reader.Read(container.containerId);
                reader.Read(container.ownerItemInstanceId);
                container.displayName = reader.ReadString();
                reader.Read(container.size);
                const auto flags = reader.Read<uint8_t>();
                container.active = (flags & 1) != 0;
                container.weaponOnly = (flags & 2) != 0;
            }
            reader.Read(state.nextInstanceId);
            reader.Read(state.nextContainerId);
            return !reader.Failed();
        }

        // What a client needs to animate, hit and judge an infected.
        void WriteZombie(FNetWriter& writer, const FZombieRuntime& z)
        {
            writer.Write(z.handle)
                .Write(z.profile)
                .Write(z.state)
                .Write(z.attack)
                .Write(z.death)
                .Write(z.position)
                .Write(z.forward)
                .Write(z.velocity)
                .Write(z.deathVelocity)
                .Write(z.staggerDirection)
                .Write(z.stateSeconds)
                .Write(z.attackTimer)
                .Write(z.corpseSeconds)
                .Write(z.flinchSeconds)
                .Write(z.health)
                .Write(z.targetId)
                .Write<uint8_t>(static_cast<uint8_t>((z.crippled ? 1 : 0) | (z.aware ? 2 : 0) | (z.seesPlayer ? 4 : 0)));
        }

        void ReadZombie(FNetReader& reader, FZombieRuntime& z)
        {
            reader.Read(z.handle);
            reader.Read(z.profile);
            reader.Read(z.state);
            reader.Read(z.attack);
            reader.Read(z.death);
            reader.Read(z.position);
            reader.Read(z.forward);
            reader.Read(z.velocity);
            reader.Read(z.deathVelocity);
            reader.Read(z.staggerDirection);
            reader.Read(z.stateSeconds);
            reader.Read(z.attackTimer);
            reader.Read(z.corpseSeconds);
            reader.Read(z.flinchSeconds);
            reader.Read(z.health);
            reader.Read(z.targetId);
            const auto flags = reader.Read<uint8_t>();
            z.crippled = (flags & 1) != 0;
            z.aware = (flags & 2) != 0;
            z.seesPlayer = (flags & 4) != 0;
            z.active = true;
        }
    }

    bool ReadHeader(FNetReader& reader, EMessage& out)
    {
        return reader.Read(out);
    }

    void Write(FNetWriter& writer, const FHello& value)
    {
        Header(writer, EMessage::Hello);
        writer.Write(value.protocol).WriteString(value.name).WriteString(value.scene);
    }

    void Write(FNetWriter& writer, const FWelcome& value)
    {
        Header(writer, EMessage::Welcome);
        writer.Write(value.protocol).Write(value.survivorId).WriteString(value.scene).Write<uint8_t>(value.pvp ? 1 : 0);
    }

    void WriteReject(FNetWriter& writer, std::string_view reason)
    {
        Header(writer, EMessage::Reject);
        writer.WriteString(reason);
    }

    void Write(FNetWriter& writer, const FPlayerState& value)
    {
        Header(writer, EMessage::PlayerState);
        writer.Write(value.presence);
        WritePresentation(writer, value.presentation);
        WriteClothing(writer, value.clothing);
    }

    void WritePlayerLeft(FNetWriter& writer, uint32_t survivorId)
    {
        Header(writer, EMessage::PlayerLeft);
        writer.Write(survivorId);
    }

    void Write(FNetWriter& writer, const FSnapshot& value)
    {
        Header(writer, EMessage::Snapshot);
        writer.Write(value.time);
        writer.Write(static_cast<uint16_t>(value.survivors.size()));
        for (const FPlayerState& survivor : value.survivors)
        {
            writer.Write(survivor.presence);
            WritePresentation(writer, survivor.presentation);
            WriteClothing(writer, survivor.clothing);
        }
        writer.Write(static_cast<uint16_t>(value.zombies.size()));
        for (const FZombieRuntime& zombie : value.zombies) WriteZombie(writer, zombie);
    }

    void Write(FNetWriter& writer, const FWeaponHitEvent& value)
    {
        Header(writer, EMessage::WeaponHit);
        writer.Write(value.sequence)
            .WriteString(value.weaponId)
            .Write(value.origin)
            .Write(value.direction)
            .Write(value.hitPoint)
            .Write(value.baseDamage)
            .Write(value.impactScale)
            .Write(value.zombie)
            .Write(value.zone)
            .Write(value.normal);
    }

    void Write(FNetWriter& writer, const FDamageNotice& value)
    {
        Header(writer, EMessage::DamageNotice);
        writer.Write(value.shooterId).Write(value.damage);
    }

    void Write(FNetWriter& writer, const FSurvivorStrike& value)
    {
        Header(writer, EMessage::SurvivorStrike);
        writer.Write(value.amount).Write(value.source);
    }

    void Write(FNetWriter& writer, const FPvpHit& value)
    {
        Header(writer, EMessage::PvpHit);
        writer.Write(value.survivorId).Write(value.amount).Write(value.source).Write(value.point);
    }

    void Write(FNetWriter& writer, const FNoise& value)
    {
        Header(writer, EMessage::Noise);
        writer.Write(value.position).Write(value.radius).Write(value.strength).Write(value.type);
    }

    void WritePvp(FNetWriter& writer, bool pvp)
    {
        Header(writer, EMessage::Pvp);
        writer.Write<uint8_t>(pvp ? 1 : 0);
    }

    void WriteLootSync(FNetWriter& writer)
    {
        Header(writer, EMessage::LootSync);
    }

    void Write(FNetWriter& writer, const FLootState& value)
    {
        Header(writer, EMessage::LootState);
        writer.Write<uint8_t>(value.full ? 1 : 0);
        writer.Write(static_cast<uint32_t>(value.entries.size()));
        for (const FLootReplica& entry : value.entries)
        {
            writer.WriteString(entry.id)
                .Write(entry.source)
                .Write(entry.position)
                .Write(entry.yaw)
                .Write<uint8_t>(entry.available ? 1 : 0);
            WriteStacks(writer, entry.contents);
        }
    }

    void Write(FNetWriter& writer, const FLootTake& value)
    {
        Header(writer, EMessage::LootTake);
        writer.Write(value.request).WriteString(value.id);
    }

    void Write(FNetWriter& writer, const FLootGrant& value)
    {
        Header(writer, EMessage::LootGrant);
        writer.Write(value.request).Write<uint8_t>(value.granted ? 1 : 0);
        WriteStacks(writer, value.contents);
    }

    void Write(FNetWriter& writer, const FLootDrop& value)
    {
        Header(writer, EMessage::LootDrop);
        writer.Write(value.position).Write(value.yaw);
        WriteStacks(writer, value.contents);
    }

    void Write(FNetWriter& writer, const FInventorySync& value)
    {
        Header(writer, EMessage::InventorySync);
        writer.Write(value.life).Write<uint8_t>(value.alive ? 1 : 0);
        WriteInventory(writer, value.inventory);
    }

    void Write(FNetWriter& writer, const FInventoryFix& value)
    {
        Header(writer, EMessage::InventoryFix);
        writer.Write(value.life).WriteString(value.reason);
        WriteInventory(writer, value.inventory);
    }

    void Write(FNetWriter& writer, const FSurvivalAction& value)
    {
        Header(writer, EMessage::SurvivalAction);
        writer.Write(value.action).Write(value.instanceId).Write(value.amount);
    }

    void Write(FNetWriter& writer, const FSurvivalState& value)
    {
        Header(writer, EMessage::SurvivalState);
        const FSurvivalSnapshot& body = value.body;
        writer.Write(value.life)
            .Write(body.health)
            .Write(body.hunger)
            .Write(body.hydration)
            .Write<int32_t>(body.bleedingWounds)
            .Write(body.cold)
            .Write(body.painkillerSeconds)
            .Write<uint8_t>(body.healing ? 1 : 0)
            .Write(body.lifeState)
            .Write(body.damageSequence)
            .Write(body.woundSequence)
            .WriteString(body.lastDamageSource);
    }

    bool Read(FNetReader& reader, FSurvivalAction& out)
    {
        reader.Read(out.action);
        reader.Read(out.instanceId);
        reader.Read(out.amount);
        return !reader.Failed();
    }

    bool Read(FNetReader& reader, FSurvivalState& out)
    {
        FSurvivalSnapshot& body = out.body;
        reader.Read(out.life);
        reader.Read(body.health);
        reader.Read(body.hunger);
        reader.Read(body.hydration);
        body.bleedingWounds = reader.Read<int32_t>();
        reader.Read(body.cold);
        reader.Read(body.painkillerSeconds);
        body.healing = reader.Read<uint8_t>() != 0;
        reader.Read(body.lifeState);
        reader.Read(body.damageSequence);
        reader.Read(body.woundSequence);
        body.lastDamageSource = reader.ReadString();
        return !reader.Failed();
    }

    bool Read(FNetReader& reader, FInventorySync& out)
    {
        reader.Read(out.life);
        out.alive = reader.Read<uint8_t>() != 0;
        return ReadInventory(reader, out.inventory);
    }

    bool Read(FNetReader& reader, FInventoryFix& out)
    {
        reader.Read(out.life);
        out.reason = reader.ReadString();
        return ReadInventory(reader, out.inventory);
    }

    bool Read(FNetReader& reader, FLootState& out)
    {
        out.full = reader.Read<uint8_t>() != 0;
        const auto count = reader.Read<uint32_t>();
        if (reader.Failed() || count > 65536) return false;
        out.entries.resize(count);
        for (FLootReplica& entry : out.entries)
        {
            entry.id = reader.ReadString();
            reader.Read(entry.source);
            reader.Read(entry.position);
            reader.Read(entry.yaw);
            entry.available = reader.Read<uint8_t>() != 0;
            if (!ReadStacks(reader, entry.contents)) return false;
        }
        return !reader.Failed();
    }

    bool Read(FNetReader& reader, FLootTake& out)
    {
        reader.Read(out.request);
        out.id = reader.ReadString();
        return !reader.Failed();
    }

    bool Read(FNetReader& reader, FLootGrant& out)
    {
        reader.Read(out.request);
        out.granted = reader.Read<uint8_t>() != 0;
        return ReadStacks(reader, out.contents);
    }

    bool Read(FNetReader& reader, FLootDrop& out)
    {
        reader.Read(out.position);
        reader.Read(out.yaw);
        return ReadStacks(reader, out.contents);
    }

    bool Read(FNetReader& reader, FHello& out)
    {
        reader.Read(out.protocol);
        out.name = reader.ReadString();
        out.scene = reader.ReadString();
        return !reader.Failed();
    }

    bool Read(FNetReader& reader, FWelcome& out)
    {
        reader.Read(out.protocol);
        reader.Read(out.survivorId);
        out.scene = reader.ReadString();
        out.pvp = reader.Read<uint8_t>() != 0;
        return !reader.Failed();
    }

    bool Read(FNetReader& reader, FPlayerState& out)
    {
        reader.Read(out.presence);
        ReadPresentation(reader, out.presentation);
        return ReadClothing(reader, out.clothing);
    }

    bool Read(FNetReader& reader, FSnapshot& out)
    {
        reader.Read(out.time);
        out.survivors.resize(reader.Read<uint16_t>());
        for (FPlayerState& survivor : out.survivors)
        {
            reader.Read(survivor.presence);
            ReadPresentation(reader, survivor.presentation);
            if (!ReadClothing(reader, survivor.clothing)) return false;
        }
        out.zombies.resize(reader.Read<uint16_t>());
        for (FZombieRuntime& zombie : out.zombies)
        {
            ReadZombie(reader, zombie);
            if (reader.Failed()) return false;
        }
        return !reader.Failed();
    }

    bool Read(FNetReader& reader, FWeaponHitEvent& out)
    {
        reader.Read(out.sequence);
        out.weaponId = reader.ReadString();
        reader.Read(out.origin);
        reader.Read(out.direction);
        reader.Read(out.hitPoint);
        reader.Read(out.baseDamage);
        reader.Read(out.impactScale);
        reader.Read(out.zombie);
        reader.Read(out.zone);
        reader.Read(out.normal);
        return !reader.Failed();
    }

    bool Read(FNetReader& reader, FDamageNotice& out)
    {
        reader.Read(out.shooterId);
        reader.Read(out.damage);
        return !reader.Failed();
    }

    bool Read(FNetReader& reader, FSurvivorStrike& out)
    {
        reader.Read(out.amount);
        reader.Read(out.source);
        return !reader.Failed();
    }

    bool Read(FNetReader& reader, FPvpHit& out)
    {
        reader.Read(out.survivorId);
        reader.Read(out.amount);
        reader.Read(out.source);
        reader.Read(out.point);
        return !reader.Failed();
    }

    bool Read(FNetReader& reader, FNoise& out)
    {
        reader.Read(out.position);
        reader.Read(out.radius);
        reader.Read(out.strength);
        reader.Read(out.type);
        return !reader.Failed();
    }
}

#endif
