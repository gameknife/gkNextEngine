#pragma once

// ============================================================================
// NetProtocol.hpp - NextDayz messages between a map's server and its clients.
// Every packet starts with one EMessage byte. Pure encode/decode over NextNet
// buffers; no sockets, no game state. Bump kProtocolVersion on any change.
// See docs/projects/nextdayz/nextdayz-multiplayer-design.md.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#if GK_MODULE_NEXTNET

#include "Modules/NextNet/NetBuffer.hpp"

#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"
#include "Application/Game/NextDayz/Combat/NoiseSystem.hpp"
#include "Application/Game/NextDayz/Inventory/LootSystem.hpp"
#include "Application/Game/NextDayz/Player/PlayerState.hpp"
#include "Application/Game/NextDayz/Player/SurvivalSystem.hpp"
#include "Application/Game/NextDayz/Sim/WorldSim.hpp"
#include "Application/Game/NextDayz/World/TimeSystem.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieSystem.hpp"

namespace NextDayz::Net
{
    inline constexpr uint16_t kProtocolVersion = 4;
    inline constexpr uint16_t kDefaultPort = 27015;
    // Survivor 1 is single player's local survivor; network survivors start here.
    inline constexpr uint32_t kFirstSurvivorId = 2;

    enum class EMessage : uint8_t
    {
        Hello = 1,       // C->S reliable: protocol, name, scene
        Welcome,         // S->C reliable: your survivor id, the map, pvp
        Reject,          // S->C reliable: why the server refused
        PlayerState,     // C->S unreliable: where I am and what my body does
        PlayerLeft,      // S->C reliable: a survivor left the world
        Snapshot,        // S->C unreliable: time, other survivors, infected
        WeaponHit,       // C->S reliable: my round hit this infected
        DamageNotice,    // S->C reliable: an infected took damage (who fired)
        SurvivorStrike,  // S->C reliable: an infected struck you
        PvpHit,          // C->S: I hit survivor X / S->C: survivor X hit you
        Noise,           // C->S reliable: I made a noise the infected may hear
        Pvp,             // S->C reliable: the server switched friendly fire
        LootSync,        // C->S reliable: my map is loaded, send me all the loot
        LootState,       // S->C reliable: loot entries (all of them, or the ones that changed)
        LootTake,        // C->S reliable: I take everything at this entry
        LootGrant,       // S->C reliable: yes (and what it held) or no (someone was faster)
        LootDrop,        // C->S reliable: put these items on the ground here
        InventorySync,   // C->S reliable: what I carry now (or that I died), per life
        InventoryFix,    // S->C reliable: refused; this is what you carry
        SurvivalAction,  // C->S reliable: I use this item / drink at a well / rest
        SurvivalState,   // S->C reliable: your body as the server keeps it
    };

    struct FHello
    {
        uint16_t protocol = kProtocolVersion;
        std::string name;
        std::string scene;
    };

    struct FWelcome
    {
        uint16_t protocol = kProtocolVersion;
        uint32_t survivorId = 0;
        std::string scene;
        bool pvp = false;
    };

    struct FPlayerState
    {
        FSurvivorPresence presence;
        FPlayerPresentationState presentation;
        std::vector<std::string> clothing;       // worn item ids, for other survivors to see
    };

    struct FSnapshot
    {
        TimeSystem::FState time;
        std::vector<FPlayerState> survivors;
        std::vector<FZombieRuntime> zombies;
    };

    struct FDamageNotice
    {
        uint32_t shooterId = 0;
        FDamageEvent damage;
    };

    struct FSurvivorStrike
    {
        float amount = 0.0f;
        glm::vec3 source{};
    };

    // Client -> server: `survivorId` is the victim. Server -> victim: the attacker.
    struct FPvpHit
    {
        uint32_t survivorId = 0;
        float amount = 0.0f;
        glm::vec3 source{};
        glm::vec3 point{};
    };

    struct FNoise
    {
        glm::vec3 position{};
        float radius = 0.0f;
        float strength = 0.0f;
        ENoiseType type = ENoiseType::Footstep;
    };

    struct FLootState
    {
        bool full = false;                       // replaces everything the client knows
        std::vector<FLootReplica> entries;
    };

    struct FLootTake
    {
        uint32_t request = 0;
        std::string id;
    };

    struct FLootGrant
    {
        uint32_t request = 0;
        bool granted = false;
        std::vector<FLootStack> contents;
    };

    struct FLootDrop
    {
        glm::vec3 position{};
        float yaw = 0.0f;
        std::vector<FLootStack> contents;
    };

    // A survivor's inventory as of one life. `life` grows with every respawn;
    // `alive` false reports the death (the server drops what it knows they carried).
    struct FInventorySync
    {
        uint32_t life = 0;
        bool alive = true;
        FInventoryState inventory;
    };

    struct FInventoryFix
    {
        uint32_t life = 0;
        std::string reason;
        FInventoryState inventory;
    };

    enum class ESurvivalAction : uint8_t
    {
        UseItem,         // eat, drink, bandage, first aid, painkillers
        DrinkFromWell,
        Rest,
        DebugHarm,       // honoured only by a server with nextdayz.net.cheats on
    };

    struct FSurvivalAction
    {
        ESurvivalAction action = ESurvivalAction::UseItem;
        FItemInstanceId instanceId = 0;
        float amount = 0.0f;
    };

    struct FSurvivalState
    {
        uint32_t life = 0;
        FSurvivalSnapshot body;
    };

    // Reads the leading message byte; false on an empty packet.
    bool ReadHeader(NextNet::FNetReader& reader, EMessage& out);

    void Write(NextNet::FNetWriter& writer, const FHello& value);
    void Write(NextNet::FNetWriter& writer, const FWelcome& value);
    void WriteReject(NextNet::FNetWriter& writer, std::string_view reason);
    void Write(NextNet::FNetWriter& writer, const FPlayerState& value);
    void WritePlayerLeft(NextNet::FNetWriter& writer, uint32_t survivorId);
    void Write(NextNet::FNetWriter& writer, const FSnapshot& value);
    void Write(NextNet::FNetWriter& writer, const FWeaponHitEvent& value);
    void Write(NextNet::FNetWriter& writer, const FDamageNotice& value);
    void Write(NextNet::FNetWriter& writer, const FSurvivorStrike& value);
    void Write(NextNet::FNetWriter& writer, const FPvpHit& value);
    void Write(NextNet::FNetWriter& writer, const FNoise& value);
    void WritePvp(NextNet::FNetWriter& writer, bool pvp);
    void WriteLootSync(NextNet::FNetWriter& writer);
    void Write(NextNet::FNetWriter& writer, const FLootState& value);
    void Write(NextNet::FNetWriter& writer, const FLootTake& value);
    void Write(NextNet::FNetWriter& writer, const FLootGrant& value);
    void Write(NextNet::FNetWriter& writer, const FLootDrop& value);
    void Write(NextNet::FNetWriter& writer, const FInventorySync& value);
    void Write(NextNet::FNetWriter& writer, const FInventoryFix& value);
    void Write(NextNet::FNetWriter& writer, const FSurvivalAction& value);
    void Write(NextNet::FNetWriter& writer, const FSurvivalState& value);

    // Bodies only (after ReadHeader). False when the packet is malformed.
    bool Read(NextNet::FNetReader& reader, FHello& out);
    bool Read(NextNet::FNetReader& reader, FWelcome& out);
    bool Read(NextNet::FNetReader& reader, FPlayerState& out);
    bool Read(NextNet::FNetReader& reader, FSnapshot& out);
    bool Read(NextNet::FNetReader& reader, FWeaponHitEvent& out);
    bool Read(NextNet::FNetReader& reader, FDamageNotice& out);
    bool Read(NextNet::FNetReader& reader, FSurvivorStrike& out);
    bool Read(NextNet::FNetReader& reader, FPvpHit& out);
    bool Read(NextNet::FNetReader& reader, FNoise& out);
    bool Read(NextNet::FNetReader& reader, FLootState& out);
    bool Read(NextNet::FNetReader& reader, FLootTake& out);
    bool Read(NextNet::FNetReader& reader, FLootGrant& out);
    bool Read(NextNet::FNetReader& reader, FLootDrop& out);
    bool Read(NextNet::FNetReader& reader, FInventorySync& out);
    bool Read(NextNet::FNetReader& reader, FInventoryFix& out);
    bool Read(NextNet::FNetReader& reader, FSurvivalAction& out);
    bool Read(NextNet::FNetReader& reader, FSurvivalState& out);
}

#endif
