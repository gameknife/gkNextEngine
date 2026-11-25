#include "Engine/Common/CoreMinimal.hpp"
#include "EconomyDefs.hpp"
#include "ItemDefs.hpp"

#include <cmath>
#include <nlohmann/json.hpp>

namespace NextDayz
{
    namespace
    {
        constexpr uint64_t maxPrice = 1000000;

        void Require(bool condition, const std::string& message)
        {
            if (!condition) throw std::runtime_error(message);
        }

        uint64_t ReadPrice(const nlohmann::json& value, const std::string& what)
        {
            Require(value.is_number_unsigned() || (value.is_number_integer() && value.get<int64_t>() >= 0),
                    what + " must be a non-negative integer");
            const auto price = value.get<uint64_t>();
            Require(price <= maxPrice, what + " is out of range");
            return price;
        }

        std::vector<uint32_t> ReadCounts(const nlohmann::json& value, const std::string& what)
        {
            Require(value.is_array(), what + " must be an array");
            std::vector<uint32_t> result;
            for (const auto& entry : value)
                result.push_back(static_cast<uint32_t>(ReadPrice(entry, what)));
            return result;
        }

        void ParseContracts(const nlohmann::json& document, FEconomyDefs& defs)
        {
            const auto& board = document.at("board");
            defs.board.slots = ReadCounts(board.at("slots"), "board.slots");
            Require(defs.board.slots.size() == defs.reputationTiers.size(), "board.slots lists every reputation tier");
            for (const auto slots : defs.board.slots) Require(slots >= 1 && slots <= 8, "board slots must be 1..8");
            defs.board.replaceAfter = static_cast<uint32_t>(ReadPrice(board.at("replaceAfter"), "board.replaceAfter"));
            defs.board.expireAfter = static_cast<uint32_t>(ReadPrice(board.at("expireAfter"), "board.expireAfter"));
            defs.board.maxActive = static_cast<uint32_t>(ReadPrice(board.at("maxActive"), "board.maxActive"));
            defs.board.deliverMultiplier = board.at("deliverMultiplier").get<double>();
            Require(defs.board.replaceAfter >= 1 && defs.board.expireAfter >= 1 && defs.board.maxActive >= 1,
                    "board cycles and maxActive must be positive");
            Require(std::isfinite(defs.board.deliverMultiplier) && defs.board.deliverMultiplier >= 1.0 &&
                    defs.board.deliverMultiplier <= 10.0, "board.deliverMultiplier must be between 1 and 10");

            static const std::map<std::string, EContractType> types = {
                {"deliver", EContractType::Deliver}, {"recon", EContractType::Recon}, {"fetch", EContractType::Fetch}};
            static const std::set<std::string> issuers = {"medical", "quartermaster", "collector", "radio"};
            static const std::set<std::string> kinds = {"hospital", "military", "fortress", "town", "industrial", "farm", "village"};
            std::set<std::string> ids;
            for (const auto& json : document.at("contracts"))
            {
                FContractTemplate entry;
                entry.id = json.at("id").get<std::string>();
                Require(!entry.id.empty() && ids.insert(entry.id).second, "contract ids must be unique: " + entry.id);
                const auto what = "contract " + entry.id;
                entry.issuer = json.at("issuer").get<std::string>();
                Require(issuers.contains(entry.issuer), what + ": unknown issuer");
                entry.title = json.at("title").get<std::string>();
                Require(!entry.title.empty(), what + ": needs a title");
                const auto type = types.find(json.at("type").get<std::string>());
                Require(type != types.end(), what + ": unknown type");
                entry.type = type->second;
                entry.tier = static_cast<uint32_t>(ReadPrice(json.at("tier"), what + ".tier"));
                Require(entry.tier >= 1 && entry.tier <= 3, what + ": tier must be 1..3");
                entry.weight = static_cast<uint32_t>(ReadPrice(json.at("weight"), what + ".weight"));
                Require(entry.weight >= 1 && entry.weight <= 100, what + ": weight must be 1..100");
                if (entry.type == EContractType::Deliver)
                {
                    for (const auto& item : json.at("items"))
                    {
                        Require(item.is_array() && item.size() == 3, what + ": items are [item, min, max]");
                        FContractItemRange range{item[0].get<std::string>(), item[1].get<int>(), item[2].get<int>()};
                        Require(FindItemDef(range.item) && !IsContractItem(range.item), what + ": unknown item " + range.item);
                        Require(range.min >= 1 && range.max >= range.min && range.max <= 200, what + ": bad item count");
                        entry.items.push_back(std::move(range));
                    }
                    Require(!entry.items.empty(), what + ": a delivery needs items");
                    entry.bonus = ReadPrice(json.at("bonus"), what + ".bonus");
                    defs.contracts.push_back(std::move(entry));
                    continue;
                }
                const auto& poi = json.at("poi");
                if (poi.contains("kind"))
                    for (const auto& kind : poi.at("kind"))
                    {
                        entry.poiKinds.push_back(kind.get<std::string>());
                        Require(kinds.contains(entry.poiKinds.back()), what + ": unknown site kind " + entry.poiKinds.back());
                    }
                entry.poiCount = poi.contains("count") ? static_cast<uint32_t>(ReadPrice(poi.at("count"), what + ".count")) : 1;
                Require(entry.poiCount >= 1 && entry.poiCount <= 2, what + ": 1 or 2 sites");
                if (poi.contains("risk"))
                {
                    const auto risk = ReadCounts(poi.at("risk"), what + ".risk");
                    Require(risk.size() == 2 && risk[0] >= 1 && risk[0] <= risk[1] && risk[1] <= 3, what + ": risk is [min, max] in 1..3");
                    entry.riskMin = risk[0];
                    entry.riskMax = risk[1];
                }
                Require(entry.type != EContractType::Fetch || entry.poiCount == 1, what + ": a fetch has one site");
                const auto reward = ReadCounts(json.at("reward"), what + ".reward");
                Require(reward.size() == 2 && reward[0] >= 1 && reward[0] <= reward[1], what + ": reward is [min, max]");
                entry.rewardMin = reward[0];
                entry.rewardMax = reward[1];
                if (entry.type == EContractType::Fetch)
                {
                    entry.parcel = json.at("parcel").get<std::string>();
                    Require(IsContractItem(entry.parcel) && FindItemDef(entry.parcel), what + ": parcel must be a contract_* item");
                }
                if (json.contains("itemReward"))
                    for (const auto& item : json.at("itemReward"))
                    {
                        entry.itemRewards.push_back(item.get<std::string>());
                        Require(FindItemDef(entry.itemRewards.back()) && !IsContractItem(entry.itemRewards.back()),
                                what + ": unknown reward item");
                    }
                defs.contracts.push_back(std::move(entry));
            }
        }

        void Parse(const nlohmann::json& document, FEconomyDefs& defs)
        {
            Require(document.is_object(), "economy table must be an object");
            static const std::set<std::string> known = {"version", "sell", "buyMarkup", "trader", "services", "reputation",
                                                        "board", "contracts"};
            for (const auto& [key, value] : document.items())
                Require(known.contains(key), "unknown economy key: " + key);
            defs.version = static_cast<uint32_t>(ReadPrice(document.at("version"), "version"));
            Require(defs.version == 1, "unsupported economy version");

            const auto& sell = document.at("sell");
            Require(sell.is_object(), "sell must be an object");
            for (const auto& [id, value] : sell.items())
            {
                Require(FindItemDef(id) != nullptr, "sell price for unknown item: " + id);
                defs.sell[id] = ReadPrice(value, "sell." + id);
                Require((defs.sell[id] == 0) == IsContractItem(id), "only contract parcels sell for 0: " + id);
            }
            for (const auto& item : AllItemDefs())
                Require(defs.sell.contains(std::string(item.id)), "no sell price for item: " + std::string(item.id));

            defs.buyMarkup = document.at("buyMarkup").get<double>();
            Require(std::isfinite(defs.buyMarkup) && defs.buyMarkup >= 2.0 && defs.buyMarkup <= 100.0,
                    "buyMarkup must be between 2 and 100");

            std::set<std::string> shelved;
            for (const auto& shelf : document.at("trader"))
            {
                FTraderEntry entry;
                entry.item = shelf.at("item").get<std::string>();
                const auto* def = FindItemDef(entry.item);
                Require(def != nullptr, "trader sells unknown item: " + entry.item);
                Require(!IsContractItem(entry.item), "the trader cannot sell contract parcels");
                Require(shelved.insert(entry.item).second, "trader lists an item twice: " + entry.item);
                entry.tier = static_cast<uint32_t>(ReadPrice(shelf.at("tier"), entry.item + ".tier"));
                entry.stock = static_cast<int>(ReadPrice(shelf.at("stock"), entry.item + ".stock"));
                entry.pack = static_cast<int>(ReadPrice(shelf.at("pack"), entry.item + ".pack"));
                Require(entry.stock > 0 && entry.stock <= 100, entry.item + ": stock must be 1..100");
                Require(entry.pack > 0 && entry.pack <= def->maxStack, entry.item + ": a pack must fit in one stack");
                const uint64_t sellPrice = defs.sell.at(entry.item);
                entry.buy = shelf.contains("buy") ? ReadPrice(shelf.at("buy"), entry.item + ".buy")
                    : static_cast<uint64_t>(std::llround(static_cast<double>(sellPrice) * defs.buyMarkup));
                Require(entry.buy > 0, entry.item + ": buy price must be positive");
                Require(entry.buy >= 2 * sellPrice, entry.item + ": buy price must be at least twice the sell price");
                defs.trader.push_back(std::move(entry));
            }

            const auto& services = document.at("services");
            defs.insertionFee = ReadPrice(services.at("insertionFee"), "insertionFee");
            const auto& warehouse = services.at("warehouse");
            defs.warehouseBase = static_cast<uint32_t>(ReadPrice(warehouse.at("base"), "warehouse.base"));
            Require(defs.warehouseBase > 0, "warehouse.base must be positive");
            uint32_t previous = defs.warehouseBase;
            for (const auto& step : warehouse.at("steps"))
            {
                Require(step.is_array() && step.size() == 2, "warehouse steps are [capacity, price]");
                FWarehouseStep parsed{static_cast<uint32_t>(ReadPrice(step[0], "warehouse capacity")),
                                      ReadPrice(step[1], "warehouse price")};
                Require(parsed.capacity > previous, "warehouse steps must grow");
                Require(parsed.price > 0, "warehouse steps must cost gold");
                previous = parsed.capacity;
                defs.warehouseSteps.push_back(parsed);
            }

            const auto& reputation = document.at("reputation");
            defs.reputationTiers = ReadCounts(reputation.at("tiers"), "reputation.tiers");
            Require(!defs.reputationTiers.empty() && defs.reputationTiers.front() == 0, "reputation tiers start at 0");
            for (size_t index = 1; index < defs.reputationTiers.size(); ++index)
                Require(defs.reputationTiers[index] > defs.reputationTiers[index - 1], "reputation tiers must grow");
            defs.reputationGain = ReadCounts(reputation.at("gain"), "reputation.gain");
            defs.reputationPenalty = static_cast<uint32_t>(ReadPrice(reputation.at("penalty"), "reputation.penalty"));
            for (const auto& entry : defs.trader)
                Require(entry.tier < defs.reputationTiers.size(), entry.item + ": tier has no reputation threshold");
            Require(defs.reputationGain.size() == 3, "reputation.gain lists tiers 1, 2 and 3");
            ParseContracts(document, defs);
        }
    }

    bool ParseEconomyDefs(std::string_view json, FEconomyDefs& defs, std::string& error)
    {
        try
        {
            FEconomyDefs candidate;
            Parse(nlohmann::json::parse(json), candidate);
            defs = std::move(candidate);
            error.clear();
            return true;
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
            return false;
        }
    }

    bool LoadEconomyDefs(const std::filesystem::path& path, FEconomyDefs& defs, std::string& error)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "cannot open " + path.string();
            return false;
        }
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        return ParseEconomyDefs(text, defs, error);
    }

    const FTraderEntry* FindTraderEntry(const FEconomyDefs& defs, std::string_view item)
    {
        const auto found = std::find_if(defs.trader.begin(), defs.trader.end(),
            [item](const auto& entry) { return entry.item == item; });
        return found == defs.trader.end() ? nullptr : &*found;
    }

    const FContractTemplate* FindContractTemplate(const FEconomyDefs& defs, std::string_view id)
    {
        const auto found = std::find_if(defs.contracts.begin(), defs.contracts.end(),
            [id](const auto& entry) { return entry.id == id; });
        return found == defs.contracts.end() ? nullptr : &*found;
    }

    const char* ContractTypeName(EContractType type)
    {
        switch (type)
        {
        case EContractType::Recon: return "recon";
        case EContractType::Fetch: return "fetch";
        default: return "deliver";
        }
    }
}
