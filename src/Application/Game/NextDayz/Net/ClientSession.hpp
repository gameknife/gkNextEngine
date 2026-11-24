#pragma once

#include "MasterApi.hpp"
#include "NetProtocol.hpp"

#include "Application/Game/NextDayz/Inventory/Inventory.hpp"

#include <map>
#include <optional>
#include <unordered_map>
#include <variant>

namespace NextDayz
{
    class PlayerActionController;
    class PlayerController;
    class SurvivalSystem;
    class WeaponRuntime;
    struct FShotEvent;
    struct FWeaponDef;
    struct FWeaponTraceEvent;
}

namespace NextDayz::Net
{
    struct FServerReject { std::string reason; };
    struct FServerPvp { bool enabled = false; };
    struct FPlayerLeft { uint32_t id = 0; };
    using FServerPacket = std::variant<std::monostate, FWelcome, FSnapshot, FLootState, FLootGrant,
        FCharacterSpawn, FExtractState, FFarewell, FSurvivalState, FInventoryFix,
        FServerReject, FServerPvp, FPlayerLeft, FDamageNotice, FSurvivorStrike, FPvpHit, FNoise>;

    struct FRemoteSurvivor
    {
        glm::vec3 feet{};
        glm::vec3 targetFeet{};
        FPlayerPresentationState presentation;
        std::vector<std::string> clothing;
        bool clothingDirty = true;
        bool alive = true;
        bool seen = false;
    };

    // The local survivor's parts that server messages and the frame gate change.
    // Desktop and Web own these objects; the session only works on them.
    struct FLocalSurvivor
    {
        Inventory& inventory;
        WeaponRuntime& weapons;
        SurvivalSystem& body;
        PlayerActionController& actions;
    };

    // Where a pick-up should land: a container, a worn slot, or a grid cell.
    struct FLootDestination
    {
        FContainerId container = 0;
        EEquipSlot slot = EEquipSlot::None;
        std::optional<FGridPlacement> placement;
    };

    struct FPendingTake
    {
        FLootReplica spot;
        FLootDestination destination;
    };

    // What one server message changed, for the client to show.
    struct FClientNews
    {
        FServerPacket packet;
        bool applied = false;               // false: malformed, for another life, or nothing waited for it
        bool replaced = false;              // spawn or fix: the inventory and weapon slots were replaced
        uint32_t request = 0;               // LootGrant: the take it answers
        std::optional<FPendingTake> take;
        std::vector<FLootStack> taken;      // LootGrant: what went into the bag
        std::vector<FLootStack> left;       // what did not fit; already sent back to the ground
        std::vector<uint32_t> removed;      // Snapshot / PlayerLeft: survivors that are gone
    };

    // Why the player cannot move or use a weapon this frame.
    struct FFrameLocks
    {
        bool action = false;       // looting, eating, healing
        bool traversal = false;    // vaulting, climbing
        bool menu = false;         // inventory, map or pause menu
        bool dead = false;
        bool Any() const { return action || traversal || menu || dead; }
    };

    struct FOutgoing
    {
        bool reliable = true;
        std::vector<std::byte> bytes;
    };

    // Shared client: Master state, the DS messages and what they do to the local
    // survivor, and the order a client frame gates input. Transports perform the
    // HTTP requests and deliver packets; desktop and Web apply presentation and
    // send what collects in `outbox`.
    class FClientSession
    {
    public:
        enum class ELobby : uint8_t { Off, Login, Waiting, Profile, Deploying, Joining, Playing };

        std::string BeginLogin(std::string_view name, std::string_view password = {});
        bool AcceptLogin(std::string_view reply);
        bool AcceptProfile(std::string_view reply);
        std::string BeginDeploy(std::string_view map, const std::vector<Master::FBring>& bring);
        bool AcceptDeploy(std::string_view reply);
        Net::FHello MakeHello(std::string_view name, std::string_view scene) const;
        bool ApplyWelcome(const Net::FWelcome& welcome);
        void ResetConnection();
        bool ShouldSendPresence(float dt);
        bool ShouldSyncInventory(float dt, bool ready);
        bool WriteInventoryIfChanged(NextNet::FNetWriter& writer, uint32_t life, const FInventoryState& state);

        std::vector<uint32_t> SynchronizeRemotes(std::span<const FPlayerState> survivors);
        void StepRemotes(float dt);
        static void ReconcileClock(TimeSystem& local, const TimeSystem::FState& remote, bool force = false);
        static FServerPacket Decode(std::span<const std::byte> bytes);

        // --- the local survivor (ClientPlay.cpp) ---

        // Decodes one server message and applies it to the survivor and the clock.
        FClientNews Receive(std::span<const std::byte> bytes, FLocalSurvivor survivor, TimeSystem& clock);
        // A life the client started itself (a direct-connect respawn).
        void BeginLife(uint32_t number);
        // Once per frame before the player moves: lock movement and drop the aim,
        // trigger and swing while busy, in a menu or dead.
        static FFrameLocks LockFrame(PlayerController& player, FLocalSurvivor survivor, bool menuOpen);
        // Mouse buttons; ignored while an action holds the hands.
        static void PressTrigger(bool down, PlayerController& player, FLocalSurvivor survivor);
        static void PressAim(bool down, PlayerController& player, FLocalSurvivor survivor);
        // Spawned with this inventory: each weapon into its usual slot, the first slot drawn.
        static void EquipCarried(WeaponRuntime& weapons, const Inventory& inventory);
        // The inventory was replaced: keep each slot whose item is still carried.
        static void KeepCarried(WeaponRuntime& weapons, const Inventory& inventory);
        // What the infected hear of a shot or swing.
        static std::optional<FNoise> ShotNoise(const FShotEvent& shot, const glm::vec3& position);

        void SyncInventory(float dt, bool ready, FLocalSurvivor survivor);
        // A freshly loaded map: the server's loot replaces whatever the client knew.
        void RequestLoot();
        uint32_t RequestTake(const FLootReplica& spot, const FLootDestination& destination = {});
        void RequestExtract(bool begin);
        // The survivor died: tell the server once and let go of what it carried.
        bool ReportDeath(FLocalSurvivor survivor);
        // Friendly fire: the remote survivor this round hit first, if any.
        std::optional<FPvpHit> FindPvpHit(const FWeaponTraceEvent& trace, const FWeaponDef& weapon) const;

        template <class Message>
        void Queue(const Message& message, bool reliable = true)
        {
            NextNet::FNetWriter writer;
            Write(writer, message);
            QueueBytes(writer, reliable);
        }
        void QueueBytes(const NextNet::FNetWriter& writer, bool reliable);

        ELobby lobby = ELobby::Off;
        std::string token;
        Master::FProfile profile;
        Master::FDeploy deploy;
        uint32_t survivorId = 0;
        std::string rejected;
        bool connected = false;
        bool pvp = false;
        std::vector<std::byte> lastInventorySync;
        std::unordered_map<uint32_t, FRemoteSurvivor> remotes;

        uint32_t life = 0;
        bool deathReported = false;
        bool lootSynced = false;
        std::map<uint32_t, FPendingTake> pendingTakes;
        uint32_t nextTake = 1;
        FExtractState extract{};
        bool extracting = false;
        bool extracted = false;
        std::string lastFix;
        uint32_t snapshots = 0, fixes = 0, takes = 0, denials = 0, hits = 0, kills = 0;
        std::vector<FOutgoing> outbox;

    private:
        void Apply(FClientNews& news, FLocalSurvivor survivor, TimeSystem& clock);

        float sendTimer_ = 0.f;
        float inventoryTimer_ = 0.f;
    };
}
