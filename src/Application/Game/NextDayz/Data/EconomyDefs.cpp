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

        void Parse(const nlohmann::json& document, FEconomyDefs& defs)
        {
            Require(document.is_object(), "economy table must be an object");
            static const std::set<std::string> known = {"version", "sell", "buyMarkup", "trader", "services", "reputation"};
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
}
