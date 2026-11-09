#pragma once

// ============================================================================
// MasterApi.hpp - The JSON the NextDayz Master (tools/gnb/internal/dzmaster)
// speaks: requests built here, replies parsed here. Pure data, no sockets.
// See docs/projects/nextdayz/nextdayz-multiplayer-design.md.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

#include "Application/Game/NextDayz/Data/LootTables.hpp"

namespace NextDayz::Master
{
    struct FMap
    {
        std::string id;
        std::string name;
        std::string scene;
        bool training = false;
    };

    // A survivor in the field, as the Master keeps it between servers.
    struct FCharacter
    {
        std::string map;
        glm::vec3 position{};
        float yaw = 0.0f;
        std::string survivalJson;            // empty: a fresh body
        std::vector<FLootStack> inventory;
        uint64_t sortie = 0;
        bool fresh = true;
    };

    struct FProfile
    {
        uint64_t playerId = 0;
        std::string name;
        int64_t gold = 0;
        std::vector<FLootStack> storage;
        std::optional<FCharacter> character;
        uint64_t sorties = 0;
        uint64_t extractions = 0;
        uint64_t deaths = 0;
        std::string lastOutcome;
        std::string playing;                 // the server holding this survivor, if any
        std::vector<FMap> maps;
    };

    struct FDeploy
    {
        std::string state;                   // "starting" or "ready"
        std::string server;
        std::string host;
        uint16_t port = 0;
        std::string ticket;
        std::string map;
        std::string scene;
        bool resume = false;
    };

    struct FBring
    {
        int index = 0;
        int count = 0;
    };

    struct FRedeem
    {
        uint64_t playerId = 0;
        std::string name;
        uint64_t sortie = 0;
        FCharacter character;
    };

    struct FHeartbeatReply
    {
        bool stop = false;
        bool pvp = false;
        std::vector<uint64_t> kick;
    };

    // How a survivor's sortie stands in a checkpoint.
    enum class EOutcome : uint8_t
    {
        Field,       // still out there
        Left,        // logged out: kept, not on any server
        Dead,        // the body stays in the world; nothing returns
        Extracted,   // what they carried goes into storage
    };

    struct FCheckpointCharacter
    {
        uint64_t playerId = 0;
        uint64_t sortie = 0;
        EOutcome outcome = EOutcome::Field;
        glm::vec3 position{};
        float yaw = 0.0f;
        std::string survivalJson;
        std::vector<FLootStack> inventory;
    };

    // --- requests ---
    std::string LoginRequest(std::string_view name);
    std::string DeployRequest(std::string_view mapId, const std::vector<FBring>& bring);
    std::string RegisterRequest(std::string_view dsId, std::string_view key, uint16_t port, std::string_view map);
    std::string HeartbeatRequest(std::string_view dsId, std::string_view key, const std::vector<uint64_t>& players,
                                 bool ready);
    std::string DsRequest(std::string_view dsId, std::string_view key);
    std::string RedeemRequest(std::string_view dsId, std::string_view key, std::string_view ticket);
    std::string CheckpointRequest(std::string_view dsId, std::string_view key, uint64_t seq,
                                  std::string_view worldJson, const std::vector<FCheckpointCharacter>& characters);

    // --- replies (false: malformed) ---
    bool ParseLogin(std::string_view json, uint64_t& playerId, std::string& token);
    bool ParseProfile(std::string_view json, FProfile& out);
    bool ParseDeploy(std::string_view json, FDeploy& out);
    bool ParseRedeem(std::string_view json, FRedeem& out);
    bool ParseHeartbeat(std::string_view json, FHeartbeatReply& out);
    // The map's world blob ("" when the Master has none yet).
    bool ParseWorld(std::string_view json, std::string& worldJson);
}
