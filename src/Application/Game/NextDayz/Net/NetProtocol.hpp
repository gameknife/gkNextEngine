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
#include "Application/Game/NextDayz/Player/PlayerState.hpp"
#include "Application/Game/NextDayz/Sim/WorldSim.hpp"
#include "Application/Game/NextDayz/World/TimeSystem.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieSystem.hpp"

namespace NextDayz::Net
{
    inline constexpr uint16_t kProtocolVersion = 1;
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
}

#endif
