#include "Engine/Common/CoreMinimal.hpp"

#include "ItemDefs.hpp"

namespace NextDayz
{
    namespace
    {
        // Volume is per stack: `maxStack` units share `volumePerStack` slots.
        constexpr std::array itemDefs = {
            FItemDef{.id = "signal_flare", .displayName = "Signal Flare Gun", .kind = EItemKind::Consumable,
                     .volumePerStack = 2, .use = EItemUse::SignalFlare,
                     .description = "Single use. Fire at a marked helipad to call the rescue helicopter."},
            // ---- firearms ----
            FItemDef{.id = "ak", .displayName = "AK-74", .kind = EItemKind::Weapon, .volumePerStack = 10,
                     .equipSlot = EEquipSlot::PrimaryWeapon, .weaponDefId = "ak",
                     .description = "Automatic rifle. 5.45x39."},
            FItemDef{.id = "svd", .displayName = "SVD", .kind = EItemKind::Weapon, .volumePerStack = 10,
                     .equipSlot = EEquipSlot::PrimaryWeapon, .weaponDefId = "svd",
                     .description = "Semi-automatic marksman rifle. 7.62x54."},
            FItemDef{.id = "mosin", .displayName = "Mosin", .kind = EItemKind::Weapon, .volumePerStack = 10,
                     .equipSlot = EEquipSlot::PrimaryWeapon, .weaponDefId = "mosin",
                     .description = "Bolt-action rifle. Hits hard, fires slow. 7.62x54."},
            FItemDef{.id = "shotgun", .displayName = "Shotgun", .kind = EItemKind::Weapon, .volumePerStack = 10,
                     .equipSlot = EEquipSlot::PrimaryWeapon, .weaponDefId = "shotgun",
                     .description = "Double-barrel. Devastating up close, very loud. 12 gauge."},
            FItemDef{.id = "pistol", .displayName = "Makarov", .kind = EItemKind::Weapon, .volumePerStack = 4,
                     .equipSlot = EEquipSlot::Sidearm, .weaponDefId = "pistol",
                     .description = "Sidearm. Quieter than a rifle. 9x18."},
            // ---- melee ----
            FItemDef{.id = "knife", .displayName = "Hunting Knife", .kind = EItemKind::Melee, .volumePerStack = 2,
                     .equipSlot = EEquipSlot::Hands, .weaponDefId = "knife", .toolCanOpener = true,
                     .description = "Fast, quiet stabs. Also opens cans."},
            FItemDef{.id = "crowbar", .displayName = "Crowbar", .kind = EItemKind::Melee, .volumePerStack = 5,
                     .equipSlot = EEquipSlot::Hands, .weaponDefId = "crowbar",
                     .description = "Heavy swings that knock the infected back."},
            FItemDef{.id = "axe", .displayName = "Fire Axe", .kind = EItemKind::Melee, .volumePerStack = 6,
                     .equipSlot = EEquipSlot::Hands, .weaponDefId = "axe", .toolCanOpener = true,
                     .description = "Slow, brutal, silent. Splits a head in one blow."},
            // ---- ammunition ----
            FItemDef{.id = "ammo_545", .displayName = "5.45x39", .kind = EItemKind::Ammo, .volumePerStack = 2, .maxStack = 30,
                     .description = "Rifle rounds for the AK-74."},
            FItemDef{.id = "ammo_762", .displayName = "7.62x54", .kind = EItemKind::Ammo, .volumePerStack = 2, .maxStack = 20,
                     .description = "Full-power rounds for the SVD and Mosin."},
            FItemDef{.id = "ammo_12g", .displayName = "12 Gauge", .kind = EItemKind::Ammo, .volumePerStack = 2, .maxStack = 12,
                     .description = "Buckshot shells."},
            FItemDef{.id = "ammo_9mm", .displayName = "9x18", .kind = EItemKind::Ammo, .volumePerStack = 1, .maxStack = 24,
                     .description = "Pistol rounds for the Makarov."},
            // ---- clothing ----
            FItemDef{.id = "helmet", .displayName = "Helmet", .kind = EItemKind::Clothing, .volumePerStack = 3,
                     .equipSlot = EEquipSlot::Head, .armor = 0.15f, .warmth = 0.05f,
                     .description = "Steel helmet. Absorbs 15% of infected damage."},
            FItemDef{.id = "jacket", .displayName = "Field Jacket", .kind = EItemKind::Clothing, .volumePerStack = 3,
                     .equipSlot = EEquipSlot::Torso, .containerCapacity = 12, .armor = 0.05f, .warmth = 0.55f,
                     .description = "Warm, with deep pockets (12)."},
            FItemDef{.id = "hoodie", .displayName = "Hoodie", .kind = EItemKind::Clothing, .volumePerStack = 2,
                     .equipSlot = EEquipSlot::Torso, .containerCapacity = 8, .warmth = 0.35f,
                     .description = "Some warmth and a front pocket (8)."},
            FItemDef{.id = "vest_military", .displayName = "Military Vest", .kind = EItemKind::Clothing, .volumePerStack = 4,
                     .equipSlot = EEquipSlot::Vest, .containerCapacity = 6, .armor = 0.3f,
                     .description = "Plate carrier. Absorbs 30% of infected damage; pouches (6)."},
            FItemDef{.id = "work_pants", .displayName = "Work Pants", .kind = EItemKind::Clothing, .volumePerStack = 2,
                     .equipSlot = EEquipSlot::Legs, .containerCapacity = 10, .warmth = 0.15f,
                     .description = "Sturdy trousers with pockets (10)."},
            FItemDef{.id = "cargo_pants", .displayName = "Cargo Pants", .kind = EItemKind::Clothing, .volumePerStack = 2,
                     .equipSlot = EEquipSlot::Legs, .containerCapacity = 12, .warmth = 0.2f,
                     .description = "Military trousers with cargo pockets (12)."},
            FItemDef{.id = "backpack_small", .displayName = "Small Backpack", .kind = EItemKind::Clothing, .volumePerStack = 3,
                     .equipSlot = EEquipSlot::Back, .containerCapacity = 16,
                     .description = "A school bag (16)."},
            FItemDef{.id = "backpack", .displayName = "Backpack", .kind = EItemKind::Clothing, .volumePerStack = 4,
                     .equipSlot = EEquipSlot::Back, .containerCapacity = 28,
                     .description = "Hiking backpack (28)."},
            FItemDef{.id = "backpack_military", .displayName = "Assault Pack", .kind = EItemKind::Clothing, .volumePerStack = 5,
                     .equipSlot = EEquipSlot::Back, .containerCapacity = 40,
                     .description = "Military pack (40)."},
            FItemDef{.id = "hat_beanie", .displayName = "Knit Beanie", .kind = EItemKind::Clothing,
                     .equipSlot = EEquipSlot::Head, .warmth = 0.18f,
                     .description = "Ribbed wool cap. Warm, with no ballistic protection."},
            FItemDef{.id = "cap_baseball", .displayName = "Baseball Cap", .kind = EItemKind::Clothing,
                     .equipSlot = EEquipSlot::Head, .warmth = 0.03f,
                     .description = "Civilian canvas cap with a curved sun visor."},
            FItemDef{.id = "hat_boonie", .displayName = "Boonie Hat", .kind = EItemKind::Clothing, .volumePerStack = 2,
                     .equipSlot = EEquipSlot::Head, .warmth = 0.06f,
                     .description = "Wide-brimmed woodland hat with a utility band."},
            FItemDef{.id = "helmet_tactical", .displayName = "Tactical Helmet", .kind = EItemKind::Clothing, .volumePerStack = 3,
                     .equipSlot = EEquipSlot::Head, .armor = 0.20f, .warmth = 0.06f,
                     .description = "Composite helmet with side rails. Absorbs 20% of infected damage."},
            FItemDef{.id = "hat_ushanka", .displayName = "Trapper Hat", .kind = EItemKind::Clothing, .volumePerStack = 2,
                     .equipSlot = EEquipSlot::Head, .warmth = 0.28f,
                     .description = "Fur-lined winter hat with lowered ear flaps."},
            FItemDef{.id = "jacket_puffer", .displayName = "Puffer Jacket", .kind = EItemKind::Clothing, .volumePerStack = 4,
                     .equipSlot = EEquipSlot::Torso, .containerCapacity = 10, .warmth = 0.75f,
                     .description = "Quilted winter insulation. Warmest outer layer; pockets (10)."},
            FItemDef{.id = "jacket_rain", .displayName = "Rain Shell", .kind = EItemKind::Clothing, .volumePerStack = 2,
                     .equipSlot = EEquipSlot::Torso, .containerCapacity = 8, .warmth = 0.25f,
                     .description = "Light storm shell with a folded hood and covered zipper; pockets (8)."},
            FItemDef{.id = "jacket_denim", .displayName = "Denim Jacket", .kind = EItemKind::Clothing, .volumePerStack = 3,
                     .equipSlot = EEquipSlot::Torso, .containerCapacity = 10, .armor = 0.03f, .warmth = 0.32f,
                     .description = "Faded blue denim with stitched chest pockets (10)."},
            FItemDef{.id = "jacket_rescue", .displayName = "Rescue Jacket", .kind = EItemKind::Clothing, .volumePerStack = 3,
                     .equipSlot = EEquipSlot::Torso, .containerCapacity = 14, .warmth = 0.48f,
                     .description = "Orange work coat with reflective bands and broad utility pockets (14)."},
            FItemDef{.id = "vest_police", .displayName = "Police Armor", .kind = EItemKind::Clothing, .volumePerStack = 4,
                     .equipSlot = EEquipSlot::Vest, .containerCapacity = 4, .armor = 0.25f,
                     .description = "Navy ballistic vest with a badge panel; pouches (4)."},
            FItemDef{.id = "vest_chest_rig", .displayName = "Chest Rig", .kind = EItemKind::Clothing, .volumePerStack = 3,
                     .equipSlot = EEquipSlot::Vest, .containerCapacity = 12, .armor = 0.08f,
                     .description = "Open shoulder harness with four magazine pouches (12). Light protection."},
            FItemDef{.id = "vest_hunting", .displayName = "Hunting Vest", .kind = EItemKind::Clothing, .volumePerStack = 3,
                     .equipSlot = EEquipSlot::Vest, .containerCapacity = 10, .warmth = 0.12f,
                     .description = "Brown field vest with cartridge loops and bellows pockets (10)."},
            FItemDef{.id = "vest_utility", .displayName = "Utility Vest", .kind = EItemKind::Clothing, .volumePerStack = 3,
                     .equipSlot = EEquipSlot::Vest, .containerCapacity = 14, .warmth = 0.05f,
                     .description = "Charcoal tool vest with an asymmetric pocket layout (14)."},
            FItemDef{.id = "vest_rescue", .displayName = "Safety Vest", .kind = EItemKind::Clothing, .volumePerStack = 2,
                     .equipSlot = EEquipSlot::Vest, .containerCapacity = 6,
                     .description = "High-visibility orange vest with reflective strips; pockets (6)."},
            FItemDef{.id = "jeans", .displayName = "Blue Jeans", .kind = EItemKind::Clothing, .volumePerStack = 2,
                     .equipSlot = EEquipSlot::Legs, .containerCapacity = 8, .warmth = 0.14f,
                     .description = "Faded denim trousers with light shoes; pockets (8)."},
            FItemDef{.id = "pants_tactical", .displayName = "Tactical Pants", .kind = EItemKind::Clothing, .volumePerStack = 3,
                     .equipSlot = EEquipSlot::Legs, .containerCapacity = 14, .armor = 0.05f, .warmth = 0.18f,
                     .description = "Sand-colored cargo trousers with strapped knee pads; pockets (14)."},
            FItemDef{.id = "pants_rain", .displayName = "Rain Trousers", .kind = EItemKind::Clothing, .volumePerStack = 2,
                     .equipSlot = EEquipSlot::Legs, .containerCapacity = 8, .warmth = 0.20f,
                     .description = "Dark storm trousers with sealed seams and rubber boots; pockets (8)."},
            FItemDef{.id = "pants_snow", .displayName = "Snow Pants", .kind = EItemKind::Clothing, .volumePerStack = 4,
                     .equipSlot = EEquipSlot::Legs, .containerCapacity = 12, .warmth = 0.45f,
                     .description = "Pale insulated trousers with reinforced knees and gaiters; pockets (12)."},
            FItemDef{.id = "backpack_messenger", .displayName = "Messenger Bag", .kind = EItemKind::Clothing, .volumePerStack = 3,
                     .equipSlot = EEquipSlot::Back, .containerCapacity = 18,
                     .description = "Wide canvas satchel with a flap and cross-body strap (18)."},
            FItemDef{.id = "backpack_medical", .displayName = "Medic Pack", .kind = EItemKind::Clothing, .volumePerStack = 4,
                     .equipSlot = EEquipSlot::Back, .containerCapacity = 24,
                     .description = "Red clamshell pack with a white first-aid cross and side pockets (24)."},
            FItemDef{.id = "backpack_rolltop", .displayName = "Rolltop Pack", .kind = EItemKind::Clothing, .volumePerStack = 4,
                     .equipSlot = EEquipSlot::Back, .containerCapacity = 32,
                     .description = "Tall teal dry bag with a rolled closure and twin buckles (32)."},
            // ---- food and drink ----
            FItemDef{.id = "food_can", .displayName = "Canned Beans", .kind = EItemKind::Consumable, .volumePerStack = 2,
                     .maxStack = 4, .hungerDelta = 32.0f, .use = EItemUse::Eat, .needsOpener = true,
                     .description = "Filling. Without a can opener or blade half of it spills."},
            FItemDef{.id = "food_tuna", .displayName = "Canned Tuna", .kind = EItemKind::Consumable, .volumePerStack = 1,
                     .maxStack = 4, .hungerDelta = 24.0f, .healthDelta = 3.0f, .use = EItemUse::Eat,
                     .needsOpener = true, .description = "Protein. Needs a can opener or blade."},
            FItemDef{.id = "food_crackers", .displayName = "Crackers", .kind = EItemKind::Consumable, .volumePerStack = 1,
                     .maxStack = 3, .hungerDelta = 15.0f, .hydrationDelta = -4.0f, .use = EItemUse::Eat,
                     .description = "Dry. Makes you thirsty."},
            FItemDef{.id = "food_apple", .displayName = "Apple", .kind = EItemKind::Consumable, .volumePerStack = 1,
                     .maxStack = 4, .hungerDelta = 9.0f, .hydrationDelta = 7.0f, .use = EItemUse::Eat,
                     .description = "A little food and a little water."},
            FItemDef{.id = "food_chocolate", .displayName = "Chocolate Bar", .kind = EItemKind::Consumable, .volumePerStack = 1,
                     .maxStack = 4, .hungerDelta = 12.0f, .healthDelta = 2.0f, .use = EItemUse::Eat,
                     .description = "Quick energy."},
            FItemDef{.id = "water_bottle", .displayName = "Water Bottle", .kind = EItemKind::Consumable, .volumePerStack = 2,
                     .hydrationDelta = 42.0f, .use = EItemUse::Drink, .leftover = "water_bottle_empty",
                     .description = "Clean water. The bottle can be refilled at a well."},
            FItemDef{.id = "water_bottle_empty", .displayName = "Empty Bottle", .kind = EItemKind::Misc, .volumePerStack = 2,
                     .description = "Fill it at a well ([F] while looking at one)."},
            FItemDef{.id = "drink_soda", .displayName = "Soda Can", .kind = EItemKind::Consumable, .volumePerStack = 1,
                     .maxStack = 4, .hungerDelta = 3.0f, .hydrationDelta = 22.0f, .use = EItemUse::Drink,
                     .description = "Warm and flat, but wet."},
            // ---- medical ----
            FItemDef{.id = "bandage", .displayName = "Bandage", .kind = EItemKind::Consumable, .volumePerStack = 1,
                     .maxStack = 4, .healthDelta = 6.0f, .use = EItemUse::Bandage,
                     .description = "Stops bleeding."},
            FItemDef{.id = "rag", .displayName = "Rags", .kind = EItemKind::Consumable, .volumePerStack = 1, .maxStack = 6,
                     .use = EItemUse::Bandage, .description = "Torn cloth. Stops bleeding."},
            FItemDef{.id = "medkit", .displayName = "First Aid Kit", .kind = EItemKind::Consumable, .volumePerStack = 3,
                     .healthDelta = 45.0f, .use = EItemUse::FirstAid,
                     .description = "Stops bleeding and restores 45 health."},
            FItemDef{.id = "painkillers", .displayName = "Painkillers", .kind = EItemKind::Consumable, .volumePerStack = 1,
                     .maxStack = 4, .use = EItemUse::Painkillers,
                     .description = "Restores 25 health over 30 seconds."},
            // ---- tools ----
            FItemDef{.id = "can_opener", .displayName = "Can Opener", .kind = EItemKind::Misc, .volumePerStack = 1,
                     .tool = EItemTool::CanOpener, .description = "Open cans without spilling them."},
            FItemDef{.id = "compass", .displayName = "Compass", .kind = EItemKind::Misc, .volumePerStack = 1,
                     .tool = EItemTool::Compass, .description = "Shows your heading while carried."},
            FItemDef{.id = "map", .displayName = "Riverland Map", .kind = EItemKind::Misc, .volumePerStack = 1,
                     .tool = EItemTool::Map, .description = "Press M to read it."},
            FItemDef{.id = "radio", .displayName = "Field Radio", .kind = EItemKind::Misc, .volumePerStack = 2,
                     .use = EItemUse::Radio,
                     .description = "Tune in to the military band (uses a battery): marks a supply cache."},
            FItemDef{.id = "battery", .displayName = "Battery", .kind = EItemKind::Misc, .volumePerStack = 1, .maxStack = 4,
                     .description = "Powers the field radio."},
            FItemDef{.id = "lantern", .displayName = "Kerosene Lantern", .kind = EItemKind::Misc, .volumePerStack = 2,
                     .use = EItemUse::Lantern,
                     .description = "Light at night (burns a fuel canister). The infected see it too."},
            FItemDef{.id = "fuel", .displayName = "Fuel Canister", .kind = EItemKind::Misc, .volumePerStack = 4,
                     .description = "Kerosene. Keeps a lantern burning for 15 minutes."},
            FItemDef{.id = "bedroll", .displayName = "Bedroll", .kind = EItemKind::Misc, .volumePerStack = 4,
                     .use = EItemUse::Rest,
                     .description = "Rest: +20 health for some food and water. Not while bleeding."},
            FItemDef{.id = "firecrackers", .displayName = "Firecrackers", .kind = EItemKind::Misc, .volumePerStack = 1,
                     .maxStack = 3, .use = EItemUse::Firecrackers,
                     .description = "Throw ahead: the bangs lure the infected away from you."},
        };
    }

    std::span<const FItemDef> AllItemDefs()
    {
        return itemDefs;
    }

    const FItemDef* FindItemDef(std::string_view id)
    {
        const auto it = std::find_if(itemDefs.begin(), itemDefs.end(),
            [id](const FItemDef& definition) { return definition.id == id; });
        return it == itemDefs.end() ? nullptr : &*it;
    }

    const FItemDef& ResolveItemDef(std::string_view id, std::string_view displayName, EItemKind fallbackKind)
    {
        if (const FItemDef* definition = FindItemDef(id))
        {
            return *definition;
        }
        static thread_local std::string fallbackId;
        static thread_local std::string fallbackDisplay;
        static thread_local FItemDef fallback;
        fallbackId = id;
        fallbackDisplay = displayName;
        fallback = FItemDef{fallbackId, fallbackDisplay, fallbackKind};
        return fallback;
    }
}
