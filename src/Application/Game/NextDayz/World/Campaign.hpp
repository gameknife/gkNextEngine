#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/Inventory/LootSystem.hpp"
#include "Application/Game/NextDayz/Player/SurvivalSystem.hpp"
#include "Application/Game/NextDayz/World/TimeSystem.hpp"
#include "Application/Game/NextDayz/Data/EconomyDefs.hpp"

namespace NextDayz
{
    enum class EFlightPhase : uint8_t
    {
        Idle,
        Inbound,
        Landing,
        Waiting,
        Departing,
    };

    struct FExtractionConfig
    {
        float arrivalSeconds = 50.0f;
        float landingSeconds = 8.0f;
        float boardingSeconds = 30.0f;
        float departureSeconds = 8.0f;
        float callRadius = 7.0f;
        float doorRadius = 2.5f;
        float autosaveSeconds = 10.0f;
    };


    struct FFlightState
    {
        EFlightPhase phase = EFlightPhase::Idle;
        std::string pointId;
        glm::vec3 pad{};
        float remaining = 0.0f;
        bool boarded = false;
    };

    // The sortie being prepared in the safehouse or played in the field. Reset
    // when the sortie ends (FinishSortie).
    struct FSortieState
    {
        uint64_t spent = 0;           // gold spent in the safehouse for this sortie
        std::string insertionMap;     // paid landing: region id, empty = random landing
        int insertionPoint = -1;      // index into the region's landing points
        uint64_t insertionPaid = 0;   // refunded when the request is cancelled
        // Save v7: contracts.
        std::vector<std::string> visitedPois;   // sites entered this sortie (names), in order
        uint64_t contractGold = 0;              // contract rewards since the last sortie ended
        uint32_t contractsCompleted = 0;
    };

    enum class EContractState : uint8_t
    {
        Offered,     // on the board
        Accepted,    // a field contract the survivor is working on
    };

    struct FContractItem
    {
        std::string item;
        int count = 1;
    };

    // One contract on the board or accepted (economy design §5). Completed,
    // expired and abandoned contracts leave the campaign; their gold and
    // reputation are what remains.
    struct FContract
    {
        uint64_t id = 0;
        std::string templateId;
        EContractType type = EContractType::Deliver;
        uint32_t tier = 1;
        std::string mapId;                  // recon / fetch
        std::vector<std::string> targets;   // site names on mapId
        std::vector<FContractItem> items;   // deliver
        std::string parcel;                 // fetch: the contract_* item planted at targets[0]
        uint64_t reward = 0;
        std::string itemReward;
        uint64_t offeredAt = 0;             // finished sorties when offered / accepted
        uint64_t acceptedAt = 0;
        EContractState state = EContractState::Offered;
    };

    // One finished sortie, for the field report and economy tuning (§8).
    struct FSortieRecord
    {
        uint64_t sortieId = 0;
        std::string mapId;
        std::string outcome;          // extracted | died | debug
        double seconds = 0.0;
        std::string deathCause;
        uint64_t extractedValue = 0;  // carried home, priced at the sell table
        uint64_t spentBefore = 0;     // trader purchases and fees before deploying
        uint64_t contractGold = 0;
        uint32_t contractsCompleted = 0;
        uint32_t reputationAfter = 0;
        std::vector<std::string> visitedPois;
    };

    struct FCampaign
    {
        bool inField = false;
        uint64_t gold = 0;
        uint64_t sorties = 0;
        uint64_t extractions = 0;
        uint64_t nextStorageId = 1;
        std::vector<FItemInstance> storage;
        FFlightState flight;
        std::unordered_map<std::string, uint64_t> mapSorties;
        std::string lastMapId;
        std::string lastOutcome;
        uint64_t sortieId = 0;
        // Save v6: the safehouse economy.
        uint32_t reputation = 0;
        std::map<std::string, int> traderStock;   // packs left this cycle
        uint64_t traderCycle = 0;                 // cycle the stock belongs to; 0 = never stocked
        FSortieState sortie;
        std::vector<FSortieRecord> history;       // most recent last
        // Save v7: the contract board.
        uint64_t boardRng = 0;                    // SplitMix64 state of board rolls
        uint64_t boardCycle = 0;                  // cycle the board was last refreshed in; 0 = never
        uint64_t contractSerial = 0;              // last contract id handed out
        std::vector<FContract> board;             // offered
        std::vector<FContract> contracts;         // accepted
        uint64_t contractsCompleted = 0;          // lifetime
        // Save v8: the warehouse (economy design §4.3). Capacity in stacks is the
        // level's (0 = base) or warehouseFloor, whichever is larger.
        uint32_t warehouseLevel = 0;
        uint32_t warehouseFloor = 0;              // a migrated save's stack count, kept until a level holds it

        // Moves an item into storage, merging plain units into existing stacks.
        bool Put(FItemInstance item);
        bool Store(Inventory& inventory, FItemInstanceId id, int count = -1);
        bool Take(Inventory& inventory, FItemInstanceId id, int count, bool wear);
        void StoreAll(Inventory& inventory);
        bool Sell(FItemInstanceId id, int count, const FEconomyDefs& economy);
        bool Call(Inventory& inventory, FItemInstanceId flareId, std::string pointId,
                  const glm::vec3& pad, const glm::vec3& player, const FExtractionConfig& config);
        bool Board(const glm::vec3& player, const FExtractionConfig& config);
        // True exactly once when an occupied helicopter finishes taking off.
        bool TickFlight(float dt, const FExtractionConfig& config);
    };

    struct FMapWorldSave
    {
        FLootWorldState world;
        TimeSystem::FState time;
        uint32_t version = 1;
    };

    struct FCampaignSave
    {
        std::string map;
        FCampaign campaign;
        FInventoryState inventory;
        FLootWorldState world;
        FSurvivalSnapshot survival;
        TimeSystem::FState time;
        glm::vec3 position{};
        float yaw = 0.0f;
        float pitch = 0.0f;
        bool firstPerson = true;
        std::array<FItemInstanceId, 3> weaponSlots{};
        int activeSlot = 0;
        int previousSlot = -1;
        double survivalSeconds = 0.0;
        uint64_t sessionRng = 1;
        uint64_t survivalRng = 1;
        std::array<std::string, 4> quickSlots{};
        bool lanternLit = false;
        float lanternSeconds = 0.0f;
        bool hasCacheMarker = false;
        glm::vec3 cacheMarker{};
        float cacheSeconds = 0.0f;
        std::unordered_map<std::string, FMapWorldSave> maps;
        uint32_t mapVersion = 1;
    };

    const char* FlightPhaseName(EFlightPhase phase);
    bool LoadExtractionConfig(const std::filesystem::path& path, FExtractionConfig& config, std::string& error);
    bool WriteCampaignSave(const std::filesystem::path& path, const FCampaignSave& save, std::string& error);
    bool ReadCampaignSave(const std::filesystem::path& path, FCampaignSave& save, std::string& error);

    // A map's world and a survivor's body as JSON, encoded as in the save (the
    // NextDayz Master stores them for its dedicated servers).
    std::string WorldToJson(const FLootWorldState& world, const TimeSystem::FState& time);
    bool WorldFromJson(std::string_view json, FLootWorldState& world, TimeSystem::FState& time, std::string& error);
    std::string SurvivalToJson(const FSurvivalSnapshot& body);
    bool SurvivalFromJson(std::string_view json, FSurvivalSnapshot& body);
}
