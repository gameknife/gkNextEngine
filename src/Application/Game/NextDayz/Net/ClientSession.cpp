#include "Engine/Common/CoreMinimal.hpp"
#include "ClientSession.hpp"

#include <algorithm>
#include <cmath>

namespace NextDayz::Net
{
    namespace
    {
        template <class Message>
        FServerPacket DecodePayload(NextNet::FNetReader& reader)
        {
            Message value{};
            if (!Read(reader, value) || !reader.AtEnd()) return {};
            return value;
        }
    }

    FServerPacket FClientSession::Decode(std::span<const std::byte> bytes)
    {
        NextNet::FNetReader reader(bytes);
        EMessage type{};
        if (!ReadHeader(reader, type)) return {};
        switch (type)
        {
        case EMessage::Welcome: return DecodePayload<FWelcome>(reader);
        case EMessage::Snapshot: return DecodePayload<FSnapshot>(reader);
        case EMessage::LootState: return DecodePayload<FLootState>(reader);
        case EMessage::LootGrant: return DecodePayload<FLootGrant>(reader);
        case EMessage::CharacterSpawn: return DecodePayload<FCharacterSpawn>(reader);
        case EMessage::ExtractState: return DecodePayload<FExtractState>(reader);
        case EMessage::Farewell: return DecodePayload<FFarewell>(reader);
        case EMessage::SurvivalState: return DecodePayload<FSurvivalState>(reader);
        case EMessage::InventoryFix: return DecodePayload<FInventoryFix>(reader);
        case EMessage::DamageNotice: return DecodePayload<FDamageNotice>(reader);
        case EMessage::SurvivorStrike: return DecodePayload<FSurvivorStrike>(reader);
        case EMessage::PvpHit: return DecodePayload<FPvpHit>(reader);
        case EMessage::Noise: return DecodePayload<FNoise>(reader);
        case EMessage::Reject:
        {
            auto reason = reader.ReadString();
            if (reader.Failed() || !reader.AtEnd()) return {};
            return FServerReject{std::move(reason)};
        }
        case EMessage::Pvp:
        {
            const auto value = reader.Read<uint8_t>();
            if (reader.Failed() || !reader.AtEnd()) return {};
            return FServerPvp{value != 0};
        }
        case EMessage::PlayerLeft:
        {
            const auto id = reader.Read<uint32_t>();
            if (reader.Failed() || !reader.AtEnd()) return {};
            return FPlayerLeft{id};
        }
        default: return {};
        }
    }

    std::string FClientSession::BeginLogin(std::string_view name, std::string_view password)
    {
        lobby = ELobby::Waiting;
        return Master::LoginRequest(name, password);
    }

    bool FClientSession::AcceptLogin(std::string_view reply)
    {
        uint64_t id = 0;
        if (!Master::ParseLogin(reply, id, token))
        {
            lobby = ELobby::Login;
            return false;
        }
        return true;
    }

    bool FClientSession::AcceptProfile(std::string_view reply)
    {
        if (!Master::ParseProfile(reply, profile)) return false;
        lobby = ELobby::Profile;
        return true;
    }

    std::string FClientSession::BeginDeploy(std::string_view map, const std::vector<Master::FBring>& bring)
    {
        lobby = ELobby::Waiting;
        return Master::DeployRequest(map, bring);
    }

    bool FClientSession::AcceptDeploy(std::string_view reply)
    {
        if (!Master::ParseDeploy(reply, deploy)) return false;
        lobby = deploy.state == "ready" ? ELobby::Joining : ELobby::Deploying;
        return true;
    }

    FHello FClientSession::MakeHello(std::string_view name, std::string_view scene) const
    {
        return {kProtocolVersion, std::string(name), std::string(scene), deploy.ticket};
    }

    bool FClientSession::ApplyWelcome(const FWelcome& welcome)
    {
        if (welcome.protocol != kProtocolVersion || welcome.survivorId == 0) return false;
        survivorId = welcome.survivorId;
        pvp = welcome.pvp;
        return true;
    }

    void FClientSession::ResetConnection()
    {
        connected = false;
        survivorId = 0;
        lastInventorySync.clear();
        pendingTakes.clear();
        lootSynced = false;
        extract = {};
        extracting = false;
        snapshots = hits = kills = 0;
        outbox.clear();
        sendTimer_ = inventoryTimer_ = 0.f;
    }

    bool FClientSession::ShouldSendPresence(float dt)
    {
        constexpr float period = 1.f / 30.f;
        sendTimer_ += dt;
        if (sendTimer_ < period) return false;
        sendTimer_ = std::fmod(sendTimer_, period);
        return true;
    }

    bool FClientSession::ShouldSyncInventory(float dt, bool ready)
    {
        inventoryTimer_ += dt;
        if (!ready || inventoryTimer_ < 0.2f) return false;
        inventoryTimer_ = 0.f;
        return true;
    }

    bool FClientSession::WriteInventoryIfChanged(NextNet::FNetWriter& writer, uint32_t life, const FInventoryState& state)
    {
        Write(writer, FInventorySync{life, true, state});
        const auto bytes = writer.Bytes();
        if (std::ranges::equal(bytes, lastInventorySync)) return false;
        lastInventorySync.assign(bytes.begin(), bytes.end());
        return true;
    }

    std::vector<uint32_t> FClientSession::SynchronizeRemotes(std::span<const FPlayerState> survivors)
    {
        for (auto& [id, remote] : remotes) remote.seen = false;
        for (const auto& survivor : survivors)
        {
            if (survivor.presence.id == survivorId) continue;
            auto [it, fresh] = remotes.try_emplace(survivor.presence.id);
            auto& remote = it->second;
            if (fresh) remote.feet = survivor.presence.feet;
            remote.targetFeet = survivor.presence.feet;
            remote.presentation = survivor.presentation;
            if (remote.clothing != survivor.clothing)
            {
                remote.clothing = survivor.clothing;
                remote.clothingDirty = true;
            }
            remote.alive = survivor.presence.alive;
            remote.seen = true;
        }

        std::vector<uint32_t> removed;
        for (auto it = remotes.begin(); it != remotes.end();)
        {
            if (it->second.seen)
            {
                ++it;
                continue;
            }
            removed.push_back(it->first);
            it = remotes.erase(it);
        }
        return removed;
    }

    void FClientSession::StepRemotes(float dt)
    {
        const float blend = 1.0f - std::exp(-14.0f * std::max(dt, 0.0f));
        for (auto& [id, remote] : remotes)
        {
            if (glm::distance(remote.feet, remote.targetFeet) > 6.0f) remote.feet = remote.targetFeet;
            remote.feet = glm::mix(remote.feet, remote.targetFeet, blend);
        }
    }

    void FClientSession::ReconcileClock(TimeSystem& local, const TimeSystem::FState& remote, bool force)
    {
        const auto current = local.Capture();
        if (force || std::abs(current.minutes - remote.minutes) > 1.0 || current.weather != remote.weather)
            local.Restore(remote);
    }
}
