#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/Net/NetProtocol.hpp"

#include <emscripten/emscripten.h>
#include <nlohmann/json.hpp>

namespace
{
    NextNet::FNetWriter reviewWriter;
    std::string reviewInspection;
}

extern "C"
{
    EMSCRIPTEN_KEEPALIVE size_t ReviewHello(const char* name, const char* ticket)
    {
        reviewWriter.Clear();
        NextDayz::Net::FHello hello;
        hello.name = name;
        hello.ticket = ticket;
        NextDayz::Net::Write(reviewWriter, hello);
        return reviewWriter.Size();
    }

    EMSCRIPTEN_KEEPALIVE size_t ReviewLootSync()
    {
        reviewWriter.Clear();
        NextDayz::Net::WriteLootSync(reviewWriter);
        return reviewWriter.Size();
    }

    EMSCRIPTEN_KEEPALIVE const std::byte* ReviewBytes() { return reviewWriter.Bytes().data(); }

    EMSCRIPTEN_KEEPALIVE const char* ReviewRead(const std::byte* bytes, size_t length)
    {
        using namespace NextDayz::Net;
        NextNet::FNetReader reader({bytes, length});
        EMessage type{};
        if (!ReadHeader(reader, type)) return nullptr;
        nlohmann::json data;
        switch (type)
        {
        case EMessage::Welcome:
        {
            FWelcome welcome;
            if (!Read(reader, welcome)) return nullptr;
            data = {{"type", "welcome"}, {"survivor", welcome.survivorId},
                    {"scene", welcome.scene}, {"protocol", welcome.protocol}};
            break;
        }
        case EMessage::Snapshot:
        {
            FSnapshot snapshot;
            if (!Read(reader, snapshot)) return nullptr;
            std::vector<uint32_t> survivors;
            for (const auto& survivor : snapshot.survivors)
            {
                survivors.push_back(survivor.presence.id);
            }
            data = {{"type", "snapshot"}, {"survivors", survivors}, {"infected", snapshot.zombies.size()}};
            break;
        }
        case EMessage::LootState:
        {
            FLootState state;
            if (!Read(reader, state) || !state.full) return nullptr;
            auto ids = nlohmann::json::array();
            for (const auto& entry : state.entries)
            {
                if (entry.source != NextDayz::ELootSource::Pile) ids.push_back(entry.id);
            }
            data = {{"type", "loot"}, {"entries", state.entries.size()}, {"bytes", length}, {"ids", std::move(ids)}};
            break;
        }
        case EMessage::CharacterSpawn:
        {
            FCharacterSpawn spawn;
            if (!Read(reader, spawn)) return nullptr;
            data = {{"type", "spawn"}, {"life", spawn.life}};
            break;
        }
        case EMessage::Reject:
            data = {{"type", "reject"}, {"message", reader.ReadString()}};
            break;
        default:
            return nullptr;
        }
        if (reader.Failed() || !reader.AtEnd()) return nullptr;
        reviewInspection = data.dump();
        return reviewInspection.c_str();
    }
}
