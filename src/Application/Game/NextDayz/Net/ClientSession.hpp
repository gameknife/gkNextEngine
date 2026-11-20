#pragma once

#include "MasterApi.hpp"
#include "NetProtocol.hpp"

#include <unordered_map>
#include <variant>

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

    // Shared client protocol and Master state. Transports perform the actual
    // HTTP requests and deliver packets; desktop and Web apply presentation.
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

    private:
        float sendTimer_ = 0.f;
        float inventoryTimer_ = 0.f;
    };
}
