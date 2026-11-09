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
        return !reader.Failed();
    }

    bool Read(FNetReader& reader, FSnapshot& out)
    {
        reader.Read(out.time);
        out.survivors.resize(reader.Read<uint16_t>());
        for (FPlayerState& survivor : out.survivors)
        {
            reader.Read(survivor.presence);
            ReadPresentation(reader, survivor.presentation);
            if (reader.Failed()) return false;
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
