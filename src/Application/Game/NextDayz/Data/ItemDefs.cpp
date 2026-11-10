#include "Engine/Common/CoreMinimal.hpp"

#include "ItemDefs.hpp"

namespace NextDayz
{
    namespace
    {
        // Footprints are per stack; quantity never changes an existing stack's rectangle.
        constexpr std::array itemDefs = {
            FItemDef{.id = "signal_flare", .displayName = "Signal Flare Gun", .kind = EItemKind::Consumable, .size = {2, 1}, .use = EItemUse::SignalFlare,
                     .description = "Single use. Fire at a marked helipad to call the rescue helicopter."},
            // ---- firearms ----
            FItemDef{.id = "optic_red_dot", .displayName = "Red Dot Sight", .kind = EItemKind::Attachment, .size = {1, 1},
                     .description = "A clear 1x red dot for close-range ADS."},
            FItemDef{.id = "optic_holographic", .displayName = "Holographic Sight", .kind = EItemKind::Attachment, .size = {2, 1},
                     .description = "A 1x holographic ring and dot for close-range ADS."},
            FItemDef{.id = "optic_4x", .displayName = "4x Scope", .kind = EItemKind::Attachment, .size = {3, 1},
                     .description = "A magnified 4x optical sight for rifles."},
            FItemDef{.id = "grip_vertical", .displayName = "Vertical Foregrip", .kind = EItemKind::Attachment, .size = {1, 2},
                     .description = "Reduces recoil by 25% and shot spread by 15%."},
            FItemDef{.id = "mag_quick", .displayName = "Quickdraw Magazine", .kind = EItemKind::Attachment, .size = {1, 2},
                     .description = "Reduces reload time by 30%."},
            FItemDef{.id = "mag_extended", .displayName = "Extended Magazine", .kind = EItemKind::Attachment, .size = {1, 2},
                     .description = "Increases magazine capacity by 50%."},
            FItemDef{.id = "muzzle_suppressor", .displayName = "Suppressor", .kind = EItemKind::Attachment, .size = {2, 1},
                     .description = "Reduces gunshot noise, muzzle flash and infected hearing range."},
            FItemDef{.id = "tactical_flashlight", .displayName = "Tactical Flashlight", .kind = EItemKind::Attachment, .size = {1, 2},
                     .description = "A weapon-mounted flashlight. Illumination will be added later."},
            FItemDef{.id = "ak", .displayName = "AK-74", .kind = EItemKind::Weapon, .size = {4, 1},
                     .equipSlot = EEquipSlot::PrimaryWeapon, .weaponDefId = "ak",
                     .description = "Automatic rifle. 5.45x39."},
            FItemDef{.id = "svd", .displayName = "SVD", .kind = EItemKind::Weapon, .size = {4, 1},
                     .equipSlot = EEquipSlot::PrimaryWeapon, .weaponDefId = "svd",
                     .description = "Semi-automatic marksman rifle. 7.62x54."},
            FItemDef{.id = "mosin", .displayName = "Mosin", .kind = EItemKind::Weapon, .size = {4, 1},
                     .equipSlot = EEquipSlot::PrimaryWeapon, .weaponDefId = "mosin",
                     .description = "Bolt-action rifle. Hits hard, fires slow. 7.62x54."},
            FItemDef{.id = "shotgun", .displayName = "Shotgun", .kind = EItemKind::Weapon, .size = {4, 1},
                     .equipSlot = EEquipSlot::PrimaryWeapon, .weaponDefId = "shotgun",
                     .description = "Double-barrel. Devastating up close, very loud. 12 gauge."},
            FItemDef{.id = "pistol", .displayName = "Makarov", .kind = EItemKind::Weapon, .size = {2, 1},
                     .equipSlot = EEquipSlot::Sidearm, .weaponDefId = "pistol",
                     .description = "Sidearm. Quieter than a rifle. 9x18."},
            // Weapons with a documented past (selected from the innawoods.net catalog).
            FItemDef{.id = "aks74u", .displayName = "AKS-74U", .kind = EItemKind::Weapon, .size = {3, 1},
                     .equipSlot = EEquipSlot::PrimaryWeapon, .weaponDefId = "aks74u",
                     .description = "Folding-stock carbine. Handy, but very loud. 5.45x39.",
                     .lore = "Adopted in 1979 for tank, helicopter and signals crews with no room for a full rifle. "
                             "The cone on the muzzle is a gas booster, not a flash hider: the short barrel needs it "
                             "to cycle, and it throws a fireball at night. Soldiers called it the 'Ksyukha'."},
            FItemDef{.id = "bizon", .displayName = "PP-19 Bizon", .kind = EItemKind::Weapon, .size = {3, 1},
                     .equipSlot = EEquipSlot::PrimaryWeapon, .weaponDefId = "bizon",
                     .description = "Submachine gun with a 64-round helical magazine. Quiet for its firepower. 9x18.",
                     .lore = "Designed at Izhmash in 1993 by Victor Kalashnikov and Alexei Dragunov, sons of the two "
                             "famous designers, around an AKS-74 receiver. The cylinder under the barrel is the "
                             "magazine: 64 Makarov rounds wound in a spiral. Interior Ministry troops carried it "
                             "through the 1990s."},
            FItemDef{.id = "aps", .displayName = "APS Stechkin", .kind = EItemKind::Weapon, .size = {2, 1},
                     .equipSlot = EEquipSlot::Sidearm, .weaponDefId = "aps",
                     .description = "Select-fire machine pistol, 20 rounds. Hard to hold on full auto. 9x18.",
                     .lore = "Igor Stechkin's pistol was adopted with the Makarov in 1951 for officers and vehicle "
                             "crews, then put into storage as too bulky to carry. The depots opened again in the "
                             "1990s, and special units gave it a second life for its automatic fire."},
            FItemDef{.id = "svt40", .displayName = "SVT-40", .kind = EItemKind::Weapon, .size = {4, 1},
                     .equipSlot = EEquipSlot::PrimaryWeapon, .weaponDefId = "svt40",
                     .description = "Self-loading rifle with a 10-round box. Powerful and loud. 7.62x54.",
                     .lore = "Fedor Tokarev's rifle was meant to replace the Mosin after the Winter War; about 1.6 "
                             "million were made before wartime factories went back to the simpler bolt-action. It "
                             "wants more cleaning than most Soviet guns. The Finns captured many and prized them."},
            FItemDef{.id = "toz34", .displayName = "TOZ-34", .kind = EItemKind::Weapon, .size = {4, 1},
                     .equipSlot = EEquipSlot::PrimaryWeapon, .weaponDefId = "toz34",
                     .description = "Over-under hunting gun. Two tightly choked barrels. 12 gauge.",
                     .lore = "Made at the Tula Arms Plant from 1964 and found in hunting lodges from the Baltic to "
                             "Kamchatka. It was the gun a Soviet hunter saved up for; the pins on the tang show "
                             "which barrel is still cocked."},
            // ---- melee ----
            FItemDef{.id = "knife", .displayName = "Hunting Knife", .kind = EItemKind::Melee, .size = {2, 1},
                     .equipSlot = EEquipSlot::Hands, .weaponDefId = "knife", .toolCanOpener = true,
                     .description = "Fast, quiet stabs. Also opens cans."},
            FItemDef{.id = "crowbar", .displayName = "Crowbar", .kind = EItemKind::Melee, .size = {4, 1},
                     .equipSlot = EEquipSlot::Hands, .weaponDefId = "crowbar",
                     .description = "Heavy swings that knock the infected back."},
            FItemDef{.id = "axe", .displayName = "Fire Axe", .kind = EItemKind::Melee, .size = {3, 2},
                     .equipSlot = EEquipSlot::Hands, .weaponDefId = "axe", .toolCanOpener = true,
                     .description = "Slow, brutal, silent. Splits a head in one blow."},
            // ---- ammunition ----
            FItemDef{.id = "ammo_545", .displayName = "5.45x39", .kind = EItemKind::Ammo, .size = {2, 1}, .maxStack = 30,
                     .description = "Rifle rounds for the AK-74 and AKS-74U."},
            FItemDef{.id = "ammo_762", .displayName = "7.62x54", .kind = EItemKind::Ammo, .size = {2, 1}, .maxStack = 20,
                     .description = "Full-power rounds for the SVD, SVT-40 and Mosin."},
            FItemDef{.id = "ammo_12g", .displayName = "12 Gauge", .kind = EItemKind::Ammo, .size = {2, 1}, .maxStack = 12,
                     .description = "Buckshot shells."},
            FItemDef{.id = "ammo_9mm", .displayName = "9x18", .kind = EItemKind::Ammo, .size = {1, 1}, .maxStack = 24,
                     .description = "Pistol rounds for the Makarov, Stechkin and Bizon."},
            // ---- clothing ----
            FItemDef{.id = "helmet", .displayName = "Helmet", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Head, .armor = 0.15f, .warmth = 0.05f,
                     .description = "Steel helmet. Absorbs 15% of infected damage."},
            FItemDef{.id = "jacket", .displayName = "Field Jacket", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Torso, .containerSize = {6, 2}, .armor = 0.05f, .warmth = 0.55f,
                     .description = "Warm, with deep pockets (12)."},
            FItemDef{.id = "hoodie", .displayName = "Hoodie", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Torso, .containerSize = {4, 2}, .warmth = 0.35f,
                     .description = "Some warmth and a front pocket (8)."},
            FItemDef{.id = "vest_military", .displayName = "Military Vest", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Vest, .containerSize = {3, 2}, .armor = 0.3f,
                     .description = "Plate carrier. Absorbs 30% of infected damage; pouches (6)."},
            FItemDef{.id = "work_pants", .displayName = "Work Pants", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Legs, .containerSize = {5, 2}, .warmth = 0.15f,
                     .description = "Sturdy trousers with pockets (10)."},
            FItemDef{.id = "cargo_pants", .displayName = "Cargo Pants", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Legs, .containerSize = {6, 2}, .warmth = 0.2f,
                     .description = "Military trousers with cargo pockets (12)."},
            FItemDef{.id = "backpack_small", .displayName = "Small Backpack", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Back, .containerSize = {4, 4},
                     .description = "A school bag (16)."},
            FItemDef{.id = "backpack", .displayName = "Backpack", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Back, .containerSize = {6, 6},
                     .description = "Hiking backpack with a 6x6 cargo grid (36)."},
            FItemDef{.id = "backpack_military", .displayName = "Assault Pack", .kind = EItemKind::Clothing, .size = {3, 2},
                     .equipSlot = EEquipSlot::Back, .containerSize = {6, 8},
                     .description = "Military pack with a 6x8 cargo grid (48)."},
            FItemDef{.id = "hat_beanie", .displayName = "Knit Beanie", .kind = EItemKind::Clothing, .size = {1, 1},
                     .equipSlot = EEquipSlot::Head, .warmth = 0.18f,
                     .description = "Ribbed wool cap. Warm, with no ballistic protection."},
            FItemDef{.id = "cap_baseball", .displayName = "Baseball Cap", .kind = EItemKind::Clothing, .size = {2, 1},
                     .equipSlot = EEquipSlot::Head, .warmth = 0.03f,
                     .description = "Civilian canvas cap with a curved sun visor."},
            FItemDef{.id = "hat_boonie", .displayName = "Boonie Hat", .kind = EItemKind::Clothing, .size = {2, 1},
                     .equipSlot = EEquipSlot::Head, .warmth = 0.06f,
                     .description = "Wide-brimmed woodland hat with a utility band."},
            FItemDef{.id = "helmet_tactical", .displayName = "Tactical Helmet", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Head, .armor = 0.20f, .warmth = 0.06f,
                     .description = "Composite helmet with side rails. Absorbs 20% of infected damage."},
            FItemDef{.id = "hat_ushanka", .displayName = "Trapper Hat", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Head, .warmth = 0.28f,
                     .description = "Fur-lined winter hat with lowered ear flaps."},
            FItemDef{.id = "jacket_puffer", .displayName = "Puffer Jacket", .kind = EItemKind::Clothing, .size = {3, 2},
                     .equipSlot = EEquipSlot::Torso, .containerSize = {5, 2}, .warmth = 0.75f,
                     .description = "Quilted winter insulation. Warmest outer layer; pockets (10)."},
            FItemDef{.id = "jacket_rain", .displayName = "Rain Shell", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Torso, .containerSize = {4, 2}, .warmth = 0.25f,
                     .description = "Light storm shell with a folded hood and covered zipper; pockets (8)."},
            FItemDef{.id = "jacket_denim", .displayName = "Denim Jacket", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Torso, .containerSize = {5, 2}, .armor = 0.03f, .warmth = 0.32f,
                     .description = "Faded blue denim with stitched chest pockets (10)."},
            FItemDef{.id = "jacket_rescue", .displayName = "Rescue Jacket", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Torso, .containerSize = {7, 2}, .warmth = 0.48f,
                     .description = "Orange work coat with reflective bands and broad utility pockets (14)."},
            FItemDef{.id = "vest_police", .displayName = "Police Armor", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Vest, .containerSize = {2, 2}, .armor = 0.25f,
                     .description = "Navy ballistic vest with a badge panel; pouches (4)."},
            FItemDef{.id = "vest_chest_rig", .displayName = "Chest Rig", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Vest, .containerSize = {6, 2}, .armor = 0.08f,
                     .description = "Open shoulder harness with four magazine pouches (12). Light protection."},
            FItemDef{.id = "vest_hunting", .displayName = "Hunting Vest", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Vest, .containerSize = {5, 2}, .warmth = 0.12f,
                     .description = "Brown field vest with cartridge loops and bellows pockets (10)."},
            FItemDef{.id = "vest_utility", .displayName = "Utility Vest", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Vest, .containerSize = {7, 2}, .warmth = 0.05f,
                     .description = "Charcoal tool vest with an asymmetric pocket layout (14)."},
            FItemDef{.id = "vest_rescue", .displayName = "Safety Vest", .kind = EItemKind::Clothing, .size = {2, 1},
                     .equipSlot = EEquipSlot::Vest, .containerSize = {3, 2},
                     .description = "High-visibility orange vest with reflective strips; pockets (6)."},
            FItemDef{.id = "jeans", .displayName = "Blue Jeans", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Legs, .containerSize = {4, 2}, .warmth = 0.14f,
                     .description = "Faded denim trousers with light shoes; pockets (8)."},
            FItemDef{.id = "pants_tactical", .displayName = "Tactical Pants", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Legs, .containerSize = {7, 2}, .armor = 0.05f, .warmth = 0.18f,
                     .description = "Sand-colored cargo trousers with strapped knee pads; pockets (14)."},
            FItemDef{.id = "pants_rain", .displayName = "Rain Trousers", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Legs, .containerSize = {4, 2}, .warmth = 0.20f,
                     .description = "Dark storm trousers with sealed seams and rubber boots; pockets (8)."},
            FItemDef{.id = "pants_snow", .displayName = "Snow Pants", .kind = EItemKind::Clothing, .size = {3, 2},
                     .equipSlot = EEquipSlot::Legs, .containerSize = {6, 2}, .warmth = 0.45f,
                     .description = "Pale insulated trousers with reinforced knees and gaiters; pockets (12)."},
            FItemDef{.id = "backpack_messenger", .displayName = "Messenger Bag", .kind = EItemKind::Clothing, .size = {3, 2},
                     .equipSlot = EEquipSlot::Back, .containerSize = {6, 3},
                     .description = "Wide canvas satchel with a flap and cross-body strap (18)."},
            FItemDef{.id = "backpack_medical", .displayName = "Medic Pack", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Back, .containerSize = {6, 4},
                     .description = "Red clamshell pack with a white first-aid cross and side pockets (24)."},
            FItemDef{.id = "backpack_rolltop", .displayName = "Rolltop Pack", .kind = EItemKind::Clothing, .size = {2, 3},
                     .equipSlot = EEquipSlot::Back, .containerSize = {4, 8},
                     .description = "Tall teal dry bag with a rolled closure and twin buckles (32)."},
            // ---- field kit with a documented past (innawoods.net selection) ----
            // Soviet Afghan war: panama, Afganka, captured chest rig, 6B3, RD-54.
            FItemDef{.id = "hat_panama", .displayName = "Panama Hat", .kind = EItemKind::Clothing, .size = {2, 1},
                     .equipSlot = EEquipSlot::Head, .warmth = 0.05f,
                     .description = "Wide-brimmed khaki field hat with a red star.",
                     .lore = "The 'panamka' was issued to troops in the hot southern districts and became the face "
                             "of the Soviet war in Afghanistan. Veterans still wear theirs, faded almost white, every "
                             "15 February: the day in 1989 the last column crossed back over the border."},
            FItemDef{.id = "helmet_ssh68", .displayName = "SSh-68 Helmet", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Head, .armor = 0.15f, .warmth = 0.05f,
                     .description = "Soviet steel helmet. Absorbs 15% of infected damage.",
                     .lore = "Adopted in 1968 and stamped out by the million, the SSh-68 covered Soviet and then "
                             "Russian heads for three decades. It stops shell splinters, not rifle bullets, and every "
                             "army depot still has stacks of them."},
            FItemDef{.id = "shirt_telnyashka", .displayName = "Telnyashka", .kind = EItemKind::Clothing, .size = {2, 1},
                     .equipSlot = EEquipSlot::Torso, .warmth = 0.15f,
                     .description = "Long-sleeved striped undershirt. Light and warm, no pockets.",
                     .lore = "Russian sailors have worn the blue-and-white stripes since 1874; in 1969 the Airborne "
                             "Forces took their own with sky-blue stripes. Under a field jacket it marked a "
                             "paratrooper or a marine. Today anyone who wants to look like one wears it."},
            FItemDef{.id = "jacket_afganka", .displayName = "Afganka Jacket", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Torso, .containerSize = {6, 2}, .armor = 0.03f, .warmth = 0.30f,
                     .description = "Sand-khaki field jacket with slanted chest pockets (12).",
                     .lore = "First issued to troops in Afghanistan in the mid-1980s and adopted army-wide as the 1988 "
                             "pattern, the 'Afganka' swapped the old tunic cut for a jacket made to fight in. The "
                             "summer cloth bleaches to pale sand within a season."},
            FItemDef{.id = "vest_type56", .displayName = "Type 56 Chest Rig", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Vest, .containerSize = {7, 2}, .armor = 0.06f,
                     .description = "Chinese canvas chest rig: three magazine pouches and four grenade pockets (14).",
                     .lore = "Shipped to the mujahideen through Pakistan, these Chinese rigs were stripped from the "
                             "dead and worn out on Soviet chests. Soldiers who could not get one sewed copies from "
                             "old rucksacks and called them the 'lifchik': the bra."},
            FItemDef{.id = "vest_6b3", .displayName = "6B3 Body Armor", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Vest, .containerSize = {4, 2}, .armor = 0.32f, .warmth = 0.05f,
                     .description = "Titanium-plated vest. Absorbs 32% of infected damage; magazine pockets (8).",
                     .lore = "Built after Afghanistan showed that the older 6B2 would not stop a rifle bullet: "
                             "overlapping titanium plates in an olive cover, four AK magazine pockets on the chest "
                             "and about twelve kilos on the shoulders."},
            FItemDef{.id = "pants_afganka", .displayName = "Afganka Trousers", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Legs, .containerSize = {5, 2}, .warmth = 0.14f,
                     .description = "Sand-khaki field trousers with thigh pockets, worn with kirza boots (10).",
                     .lore = "Roomy thigh pockets for the mountains, tucked into 'kirza' boots of pressed artificial "
                             "leather: the cheap, waterproof Soviet substitute that shod the army for half a century."},
            FItemDef{.id = "backpack_rd54", .displayName = "RD-54 Airborne Pack", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Back, .containerSize = {5, 5},
                     .description = "Olive canvas paratrooper pack with outside pockets (25).",
                     .lore = "The 'paratrooper's pack, model 1954' was cut to ride under a parachute harness, with "
                             "outside pockets for magazines, grenades and the entrenching tool. Generations of "
                             "conscripts carried everything they owned in one."},
            // Mountain troops and hunters: the Gorka suit and the Abalakov rucksack.
            FItemDef{.id = "jacket_gorka", .displayName = "Gorka Anorak", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Torso, .containerSize = {6, 2}, .armor = 0.04f, .warmth = 0.45f,
                     .description = "Windproof tent-cloth smock with a hood and reinforced elbows (12).",
                     .lore = "Developed in the late 1970s for Soviet mountain troops; 'gorka' means 'little hill'. "
                             "The thick canvas shell with brown reinforcements went on to the Caucasus with special "
                             "forces, and then to every hunter, fisherman and mushroom picker in the country."},
            FItemDef{.id = "pants_gorka", .displayName = "Gorka Trousers", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Legs, .containerSize = {5, 2}, .armor = 0.03f, .warmth = 0.25f,
                     .description = "Tent-cloth trousers reinforced at the knees and seat (10).",
                     .lore = "Cut loose and doubled at the knees and seat for crawling over rock, with cuffs that "
                             "keep the snow out of the boots. The pattern has been sewn, copied and counterfeited "
                             "for forty years."},
            FItemDef{.id = "backpack_abalakov", .displayName = "Abalakov Rucksack", .kind = EItemKind::Clothing, .size = {3, 2},
                     .equipSlot = EEquipSlot::Back, .containerSize = {7, 5},
                     .description = "Wide canvas tourist rucksack with leather straps (35).",
                     .lore = "Designed by the mountaineer Vitaly Abalakov in the 1930s and made with little change "
                             "for half a century, the wide canvas 'abalakovsky' went on every Soviet hike, geology "
                             "expedition and dacha trip. It sags and rides low, but it swallows everything."},
            // East German National People's Army (NVA).
            FItemDef{.id = "helmet_m56", .displayName = "M56 Stahlhelm", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Head, .armor = 0.17f, .warmth = 0.05f,
                     .description = "Flared East German steel helmet. Absorbs 17% of infected damage.",
                     .lore = "Designed during the Second World War as a lighter Wehrmacht helmet and shelved; the "
                             "East German army adopted it in 1956 precisely because it did not look like the old "
                             "one. After 1990 the NVA's stocks were sold off across Central Europe."},
            FItemDef{.id = "jacket_strichtarn", .displayName = "Strichtarn Jacket", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Torso, .containerSize = {5, 2}, .armor = 0.03f, .warmth = 0.34f,
                     .description = "East German rain-pattern field jacket (10).",
                     .lore = "The NVA printed its field uniforms in 'Strichtarn' from 1965 until 1990: fine brown "
                             "rain strokes on grey-green, nicknamed 'one stroke, no stroke'. The Czechoslovak army "
                             "across the border wore a close cousin, the needle pattern."},
            // Printed camouflage (gk_texture prints): M81 Woodland, EMR, DBDU, VSR-98 Flora.
            FItemDef{.id = "helmet_pasgt", .displayName = "PASGT Helmet", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Head, .armor = 0.22f, .warmth = 0.06f,
                     .description = "Kevlar helmet under a woodland cover. Absorbs 22% of infected damage.",
                     .lore = "The US Army's Personnel Armor System for Ground Troops replaced the steel M1 in the early "
                             "1980s. Its Kevlar shell first saw combat on Grenada in 1983, and soldiers called it the "
                             "'Fritz' for its likeness to the old German helmet."},
            FItemDef{.id = "hat_boonie_emr", .displayName = "EMR Boonie Hat", .kind = EItemKind::Clothing, .size = {2, 1},
                     .equipSlot = EEquipSlot::Head, .warmth = 0.05f,
                     .description = "Soft wide-brimmed hat in Russian digital camouflage, with a foliage band.",
                     .lore = "The army 'panama' again, now in the 2008 digital print: the same shade-giving brim as "
                             "the Afghan-war panamka, with loops on the band for grass and twigs."},
            FItemDef{.id = "jacket_m81", .displayName = "M81 Woodland Jacket", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Torso, .containerSize = {6, 2}, .armor = 0.04f, .warmth = 0.34f,
                     .description = "Woodland BDU coat with four flapped pockets and reinforced elbows (12).",
                     .lore = "The Battle Dress Uniform arrived in 1981 printed in M81 Woodland, an enlarged version "
                             "of the ERDL pattern from Vietnam. For two decades it was the American soldier's look, "
                             "and surplus carried it to armies, militias and hunters around the world."},
            FItemDef{.id = "pants_m81", .displayName = "M81 Woodland Trousers", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Legs, .containerSize = {6, 2}, .armor = 0.03f, .warmth = 0.18f,
                     .description = "BDU trousers with cargo pockets and doubled knees, worn with black combat boots (12).",
                     .lore = "Cut with double-layer knees and seat and big cargo pockets, bloused over black leather "
                             "boots. The temperate cloth was a nylon-cotton twill; a lighter cotton ripstop followed "
                             "for hot climates."},
            FItemDef{.id = "jacket_emr", .displayName = "EMR Field Jacket", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Torso, .containerSize = {6, 2}, .armor = 0.04f, .warmth = 0.36f,
                     .description = "Russian digital-camouflage jacket with zipped chest pockets and arm pockets (12).",
                     .lore = "In 2008 the Russian army changed into a uniform from the fashion house of Valentin "
                             "Yudashkin, printed in the Unified Camouflage Pattern (EMR) that every soldier simply "
                             "calls 'Tsifra': the digital."},
            FItemDef{.id = "pants_emr", .displayName = "EMR Field Trousers", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Legs, .containerSize = {6, 2}, .armor = 0.04f, .warmth = 0.20f,
                     .description = "Digital-camouflage trousers with cargo pockets and knee-pad pockets, worn with laced boots (12).",
                     .lore = "The trousers take insert knee pads and tuck into laced 'bertsy': the boots that, with "
                             "this uniform, finally retired the kirza and the footwrap."},
            FItemDef{.id = "jacket_dbdu", .displayName = "DBDU Desert Jacket", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Torso, .containerSize = {6, 2}, .armor = 0.03f, .warmth = 0.26f,
                     .description = "Six-color 'chocolate chip' desert coat in the BDU cut (12).",
                     .lore = "Issued for desert operations from the early 1980s, the six-color pattern went to war in "
                             "the Gulf in 1991. Its dark rock spots stood out on the pale sand there, and a plainer "
                             "three-color desert uniform replaced it soon after."},
            FItemDef{.id = "pants_dbdu", .displayName = "DBDU Desert Trousers", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Legs, .containerSize = {6, 2}, .warmth = 0.14f,
                     .description = "Chocolate-chip trousers with cargo pockets, worn with tan suede desert boots (12).",
                     .lore = "Worn with tan suede desert boots instead of black leather: black boots soaked up the "
                             "sun and showed every scuff of sand."},
            FItemDef{.id = "jacket_flora", .displayName = "VSR-98 Flora Jacket", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Torso, .containerSize = {6, 2}, .armor = 0.03f, .warmth = 0.30f,
                     .description = "Russian 'watermelon' camouflage field jacket in the Afganka cut (12).",
                     .lore = "Adopted in 1998 and worn through the Second Chechen War, Flora's vertical green stripes "
                             "earned it the nickname 'watermelon'. It was printed on the Afganka cut until the "
                             "digital uniform took over in 2008."},
            FItemDef{.id = "pants_flora", .displayName = "VSR-98 Flora Trousers", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Legs, .containerSize = {5, 2}, .warmth = 0.16f,
                     .description = "Flora-pattern trousers with thigh pockets and knee pads, worn with laced boots (10).",
                     .lore = "The same cut as the Afganka trousers. By the late 1990s laced combat boots had begun to "
                             "push the kirza out of combat units."},
            FItemDef{.id = "pants_strichtarn", .displayName = "Strichtarn Trousers", .kind = EItemKind::Clothing, .size = {2, 2},
                     .equipSlot = EEquipSlot::Legs, .containerSize = {4, 2}, .warmth = 0.18f,
                     .description = "Rain-pattern field trousers with high marching boots (8).",
                     .lore = "Issued with the jacket to East German conscripts and worn over high leather marching "
                             "boots, which the NVA kept long after most armies had moved to laced ones."},
            // ---- food and drink ----
            FItemDef{.id = "food_can", .displayName = "Canned Beans", .kind = EItemKind::Consumable, .size = {1, 2},
                     .maxStack = 4, .hungerDelta = 32.0f, .use = EItemUse::Eat, .needsOpener = true,
                     .description = "Filling. Without a can opener or blade half of it spills."},
            FItemDef{.id = "food_tuna", .displayName = "Canned Tuna", .kind = EItemKind::Consumable, .size = {1, 1},
                     .maxStack = 4, .hungerDelta = 24.0f, .healthDelta = 3.0f, .use = EItemUse::Eat,
                     .needsOpener = true, .description = "Protein. Needs a can opener or blade."},
            FItemDef{.id = "food_crackers", .displayName = "Crackers", .kind = EItemKind::Consumable, .size = {2, 1},
                     .maxStack = 3, .hungerDelta = 15.0f, .hydrationDelta = -4.0f, .use = EItemUse::Eat,
                     .description = "Dry. Makes you thirsty."},
            FItemDef{.id = "food_apple", .displayName = "Apple", .kind = EItemKind::Consumable, .size = {1, 1},
                     .maxStack = 4, .hungerDelta = 9.0f, .hydrationDelta = 7.0f, .use = EItemUse::Eat,
                     .description = "A little food and a little water."},
            FItemDef{.id = "food_chocolate", .displayName = "Chocolate Bar", .kind = EItemKind::Consumable, .size = {1, 1},
                     .maxStack = 4, .hungerDelta = 12.0f, .healthDelta = 2.0f, .use = EItemUse::Eat,
                     .description = "Quick energy."},
            FItemDef{.id = "water_bottle", .displayName = "Water Bottle", .kind = EItemKind::Consumable, .size = {1, 2},
                     .hydrationDelta = 42.0f, .use = EItemUse::Drink, .leftover = "water_bottle_empty",
                     .description = "Clean water. The bottle can be refilled at a well."},
            FItemDef{.id = "water_bottle_empty", .displayName = "Empty Bottle", .kind = EItemKind::Misc, .size = {1, 2},
                     .description = "Fill it at a well ([F] while looking at one)."},
            FItemDef{.id = "drink_soda", .displayName = "Soda Can", .kind = EItemKind::Consumable, .size = {1, 1},
                     .maxStack = 4, .hungerDelta = 3.0f, .hydrationDelta = 22.0f, .use = EItemUse::Drink,
                     .description = "Warm and flat, but wet."},
            // ---- medical ----
            FItemDef{.id = "bandage", .displayName = "Bandage", .kind = EItemKind::Consumable, .size = {1, 1},
                     .maxStack = 4, .healthDelta = 6.0f, .use = EItemUse::Bandage,
                     .description = "Stops bleeding."},
            FItemDef{.id = "rag", .displayName = "Rags", .kind = EItemKind::Consumable, .size = {1, 1}, .maxStack = 6,
                     .use = EItemUse::Bandage, .description = "Torn cloth. Stops bleeding."},
            FItemDef{.id = "medkit", .displayName = "First Aid Kit", .kind = EItemKind::Consumable, .size = {3, 1},
                     .healthDelta = 45.0f, .use = EItemUse::FirstAid,
                     .description = "Stops bleeding and restores 45 health."},
            FItemDef{.id = "painkillers", .displayName = "Painkillers", .kind = EItemKind::Consumable, .size = {1, 1},
                     .maxStack = 4, .use = EItemUse::Painkillers,
                     .description = "Restores 25 health over 30 seconds."},
            // ---- tools ----
            FItemDef{.id = "can_opener", .displayName = "Can Opener", .kind = EItemKind::Misc, .size = {1, 1},
                     .tool = EItemTool::CanOpener, .description = "Open cans without spilling them."},
            FItemDef{.id = "compass", .displayName = "Compass", .kind = EItemKind::Misc, .size = {1, 1},
                     .tool = EItemTool::Compass, .description = "Shows your heading while carried."},
            FItemDef{.id = "map", .displayName = "Riverland Map", .kind = EItemKind::Misc, .size = {2, 1},
                     .tool = EItemTool::Map, .description = "Press M to read it."},
            FItemDef{.id = "radio", .displayName = "Field Radio", .kind = EItemKind::Misc, .size = {2, 1},
                     .use = EItemUse::Radio,
                     .description = "Tune in to the military band (uses a battery): marks a supply cache."},
            FItemDef{.id = "battery", .displayName = "Battery", .kind = EItemKind::Misc, .size = {1, 1}, .maxStack = 4,
                     .description = "Powers the field radio."},
            FItemDef{.id = "lantern", .displayName = "Kerosene Lantern", .kind = EItemKind::Misc, .size = {2, 2},
                     .use = EItemUse::Lantern,
                     .description = "Light at night (burns a fuel canister). The infected see it too."},
            FItemDef{.id = "fuel", .displayName = "Fuel Canister", .kind = EItemKind::Misc, .size = {2, 2},
                     .description = "Kerosene. Keeps a lantern burning for 15 minutes."},
            FItemDef{.id = "bedroll", .displayName = "Bedroll", .kind = EItemKind::Misc, .size = {3, 2},
                     .use = EItemUse::Rest,
                     .description = "Rest: +20 health for some food and water. Not while bleeding."},
            FItemDef{.id = "firecrackers", .displayName = "Firecrackers", .kind = EItemKind::Misc, .size = {1, 1},
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
