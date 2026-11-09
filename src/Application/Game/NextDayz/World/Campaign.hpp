#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/Inventory/LootSystem.hpp"
#include "Application/Game/NextDayz/Player/SurvivalSystem.hpp"
#include "Application/Game/NextDayz/World/TimeSystem.hpp"

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
        std::unordered_map<std::string, int> sellPrices;
    };

    struct FFlightState
    {
        EFlightPhase phase = EFlightPhase::Idle;
        std::string pointId;
        glm::vec3 pad{};
        float remaining = 0.0f;
        bool boarded = false;
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

        bool Store(Inventory& inventory, FItemInstanceId id, int count = -1);
        bool Take(Inventory& inventory, FItemInstanceId id, int count, bool wear);
        void StoreAll(Inventory& inventory);
        bool Sell(FItemInstanceId id, int count, const FExtractionConfig& config);
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

    int SellPrice(std::string_view itemId, const FExtractionConfig& config);
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
