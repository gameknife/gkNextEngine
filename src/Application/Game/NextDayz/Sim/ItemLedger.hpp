#pragma once

// ============================================================================
// ItemLedger.hpp - Item conservation for a networked survivor. The server
// keeps, per survivor, the totals it knows they carry; a client's reported
// inventory is accepted only if it holds no more of anything than those totals
// plus what the server granted, apart from the game's real conversions:
//   * an item used up may leave its `leftover` (water bottle -> empty bottle),
//   * an empty bottle may be filled at a well (empty bottle -> water bottle).
// Rounds in a magazine count as their ammo item, attachments on a weapon as
// their attachment item, so reloading and fitting parts conserve totals.
// Pure logic: no sockets, no scene. See nextdayz-multiplayer-design.md.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include <map>
#include <span>

#include "Application/Game/NextDayz/Data/LootTables.hpp"
#include "Application/Game/NextDayz/Inventory/Inventory.hpp"

namespace NextDayz
{
    using FItemTotals = std::map<std::string, int64_t>;

    // Everything an inventory holds, counted by item id (see the header comment).
    FItemTotals CountItems(const FInventoryState& inventory);
    FItemTotals CountItems(std::span<const FLootStack> stacks);
    void AddTotals(FItemTotals& into, const FItemTotals& more);
    // Removes `less` from `from`; false (and `from` untouched) if `from` lacks any of it.
    bool SubtractTotals(FItemTotals& from, const FItemTotals& less);

    struct FLedgerVerdict
    {
        bool accepted = false;
        std::string reason;   // what was gained from nothing, when refused
    };

    // May a survivor known to carry `allowed` now report `reported`?
    FLedgerVerdict CheckConservation(const FItemTotals& allowed, const FItemTotals& reported);

    // What a fresh survivor may start a life with: the pockets kit, plus the
    // testing kit (backpack, AK with a full magazine and 60 rounds) when on.
    FItemTotals StartingKit(bool testingKit);

    // An inventory's items as ground stacks (a survivor's body, a dropped bag).
    std::vector<FLootStack> InventoryAsStacks(const FInventoryState& inventory);
}
