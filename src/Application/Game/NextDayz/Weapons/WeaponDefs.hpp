#pragma once

// ============================================================================
// WeaponDefs.hpp - Static weapon / ammo data tables (constexpr): ten firearms
// and three melee weapons. Runtime state (mag counts, slots) lives in
// WeaponSystem; this file is pure data + lookup helpers.
// ============================================================================

#include <array>
#include <cstdint>
#include <string_view>

namespace NextDayz
{
    inline constexpr int kWeaponSlotCount = 3;
    inline constexpr int kSidearmSlot = 2;
    enum class EAmmoType
    {
        Rifle545,
        Rifle762,
        Shotgun12,
        Pistol9,
        Count,
        None = Count, // melee
    };

    // Inventory item id used to store this ammo as a stackable reserve.
    constexpr std::string_view AmmoItemId(EAmmoType type)
    {
        switch (type)
        {
        case EAmmoType::Rifle545:  return "ammo_545";
        case EAmmoType::Rifle762:  return "ammo_762";
        case EAmmoType::Shotgun12: return "ammo_12g";
        case EAmmoType::Pistol9:   return "ammo_9mm";
        default:                   return "ammo_unknown";
        }
    }

    constexpr std::string_view AmmoDisplayName(EAmmoType type)
    {
        switch (type)
        {
        case EAmmoType::Rifle545:  return "5.45x39";
        case EAmmoType::Rifle762:  return "7.62x54";
        case EAmmoType::Shotgun12: return "12 Gauge";
        case EAmmoType::Pistol9:   return "9x18";
        default:                   return "Ammo";
        }
    }

    // Handling class shared by guns that are not individually special-cased:
    // shot sound, muzzle flash, infected noise type, sidearm slot and ADS speed.
    enum class EWeaponFamily : uint8_t
    {
        Rifle,     // assault rifles and carbines
        Smg,       // pistol-calibre shoulder weapons
        Marksman,  // full-power rifle cartridges
        Shotgun,
        Sidearm,   // fits the holster slot
        Melee,
    };

    struct FWeaponDef
    {
        std::string_view id;          // "ak" / "svd" / "mosin" / "shotgun" / "pistol"
        std::string_view displayName; // "AK-74"
        EWeaponFamily family;
        EAmmoType ammo;
        int   magSize;
        float fireInterval;           // seconds between shots (RPM converted)
        bool  fullAuto;
        float spreadHip;              // radians of random cone, hip fire
        float spreadAds;              // radians of random cone, aimed
        float adsFov;                 // first-person iron-sight FOV (vertical degrees); optics override it
        float cameraKickDegrees;      // upward camera velocity impulse
        float cameraYawDegrees;       // deterministic signed horizontal impulse range
        float viewModelKick;          // backward local offset velocity impulse
        float rigRecoilScale;         // TPS additive layer weight
        float baseDamage;
        int pellets;
        float damageFalloffDistance;
        float noiseRadius;
        float impactScale;            // poise damage per point of damage (stagger power)
        float meleeRange = 0.0f;      // > 0: a melee weapon; swings reach this far, no ammo
        float meleeStrikeTime01 = 0.0f; // contact frame in the independent attack clip
        bool IsMelee() const { return meleeRange > 0.0f; }
        bool IsSidearm() const { return family == EWeaponFamily::Sidearm; }
    };

    inline constexpr std::array<FWeaponDef, 13> kWeapons = {{
        // id        display     family                  ammo                 mag interval auto hip      ads      fov   camUp yaw vm   rig  dmg pel fall noise impact
        // Iron sights only magnify a little (about 1.15-1.35x of the 75 degree view); a 4x optic is the real zoom.
        {"ak",      "AK-74",    EWeaponFamily::Rifle,    EAmmoType::Rifle545,  30, 0.092f, true, 0.035f, 0.006f, 64.0f, 1.7f, 0.35f, 0.12f, 1.0f, 34.0f, 1, 110.0f, 110.0f, 0.5f},
        {"svd",     "SVD",      EWeaponFamily::Marksman, EAmmoType::Rifle762,  10, 0.28f,  false,0.020f, 0.0015f,62.0f, 3.2f, 0.55f, 0.20f, 1.25f, 72.0f, 1, 180.0f, 120.0f, 1.0f},
        {"mosin",   "Mosin",    EWeaponFamily::Marksman, EAmmoType::Rifle762,   5, 0.95f,  false,0.018f, 0.0012f,60.0f, 4.0f, 0.45f, 0.24f, 1.35f, 86.0f, 1, 170.0f, 125.0f, 1.0f},
        {"shotgun", "Shotgun",  EWeaponFamily::Shotgun,  EAmmoType::Shotgun12,  6, 0.80f,  false,0.090f, 0.055f, 68.0f, 5.0f, 0.65f, 0.30f, 1.5f, 18.0f, 8, 45.0f, 130.0f, 1.0f},
        {"pistol",  "Makarov",  EWeaponFamily::Sidearm,  EAmmoType::Pistol9,   12, 0.18f,  false,0.045f, 0.020f, 66.0f, 1.3f, 0.40f, 0.09f, 0.8f, 26.0f, 1, 65.0f, 55.0f, 0.6f},
        // Field finds with a history (innawoods-inspired set). Short barrels shed
        // velocity: the carbine trades range and quiet for handiness, the Bizon
        // spends cheap 9x18 by the drum, the Stechkin is a machine pistol in a holster.
        {"aks74u",  "AKS-74U",  EWeaponFamily::Rifle,    EAmmoType::Rifle545,  30, 0.082f, true, 0.040f, 0.009f, 66.0f, 1.9f, 0.45f, 0.12f, 1.0f, 30.0f, 1, 80.0f, 120.0f, 0.45f},
        {"bizon",   "PP-19 Bizon", EWeaponFamily::Smg,   EAmmoType::Pistol9,   64, 0.088f, true, 0.030f, 0.011f, 66.0f, 1.0f, 0.30f, 0.08f, 0.8f, 22.0f, 1, 55.0f, 72.0f, 0.45f},
        {"aps",     "APS Stechkin", EWeaponFamily::Sidearm, EAmmoType::Pistol9, 20, 0.080f, true, 0.050f, 0.024f, 66.0f, 1.6f, 0.60f, 0.10f, 0.85f, 25.0f, 1, 60.0f, 58.0f, 0.55f},
        {"svt40",   "SVT-40",   EWeaponFamily::Marksman, EAmmoType::Rifle762,  10, 0.24f,  false,0.022f, 0.0018f,61.0f, 3.6f, 0.60f, 0.22f, 1.3f, 76.0f, 1, 170.0f, 128.0f, 1.0f},
        {"toz34",   "TOZ-34",   EWeaponFamily::Shotgun,  EAmmoType::Shotgun12,  2, 0.30f,  false,0.080f, 0.045f, 68.0f, 5.2f, 0.60f, 0.32f, 1.5f, 19.0f, 8, 55.0f, 130.0f, 1.0f},
        // Melee: complete wind-up / contact / recovery cycle; no gun recoil or ADS zoom.
        {"knife",   "Hunting Knife", EWeaponFamily::Melee, EAmmoType::None,     0, 0.42f,  false,0.0f, 0.0f, 75.0f, 0.0f, 0.0f, 0.0f, 0.0f, 38.0f, 1, 10.0f,  6.0f, 0.9f, 1.7f, 0.38f},
        {"crowbar", "Crowbar",  EWeaponFamily::Melee,    EAmmoType::None,       0, 0.78f,  false,0.0f, 0.0f, 75.0f, 0.0f, 0.0f, 0.0f, 0.0f, 52.0f, 1, 10.0f,  9.0f, 1.6f, 2.0f, 0.42f},
        {"axe",     "Fire Axe", EWeaponFamily::Melee,    EAmmoType::None,       0, 1.05f,  false,0.0f, 0.0f, 75.0f, 0.0f, 0.0f, 0.0f, 0.0f, 85.0f, 1, 10.0f,  9.0f, 1.8f, 2.1f, 0.43f},
    }};

    // Returns nullptr for an unknown id.
    inline const FWeaponDef* FindWeaponDef(std::string_view id)
    {
        for (const FWeaponDef& def : kWeapons)
        {
            if (def.id == id)
            {
                return &def;
            }
        }
        return nullptr;
    }

    // Material slots of one injected weapon model (one per mesh section).
    using FWeaponMaterialSlots = std::array<uint32_t, 16>;

    // The FPS view model is drawn at this scale (FWeaponFeelConfig::ViewModelScale)
    // so that it stays out of walls; eye reliefs below are real camera distances.
    inline constexpr float kViewModelScale = 0.65f;

    // Landmarks of the weapon models in their own space (nextdayz_weapons.scad:
    // origin on the firing grip, +Z muzzle, +Y up). Muzzle effects start at
    // (0, boreHeight, muzzleForward); the first-person ADS pose puts sightHeight
    // on the eye line. Keep in step with cw_wpn_*_held in kit_coldwar.scad.
    struct FWeaponModelPoints
    {
        float muzzleForward;
        float boreHeight;
        float sightHeight;
        float rearSightForward = 0.0f;
        // Where the eye sits along the gun for iron sights and 1x optics (model
        // space). Like PUBG it is over the back of the receiver rather than at a
        // true cheek weld: with a 75 degree view, an eye further back looks down
        // the whole stock (the old fixed relief put it on the butt plate).
        float eyeForward = 0.0f;
        float adsEyeRelief = 0.0f; // camera -> rear sight / eyepiece, metres after view-model scaling
        float scopeFov = 0.0f;     // > 0: magnified optic, replaces the mesh with a lens view
    };

    constexpr FWeaponModelPoints EyeStation(FWeaponModelPoints points)
    {
        points.adsEyeRelief = (points.rearSightForward - points.eyeForward) * kViewModelScale;
        return points;
    }

    constexpr FWeaponModelPoints WeaponModelPoints(std::string_view id)
    {
        // PSO-1 eyecup / 4x view (explicit relief); 1x optics sit over the receiver.
        if (id == "svd") return {0.844f, 0.066f, 0.128f, -0.188f, -0.110f, 0.070f, 22.0f};
        // Far enough back that the cocking knob sits low in the picture.
        if (id == "mosin") return EyeStation({0.822f, 0.070f, 0.094f, 0.211f, -0.250f});
        // The eye rides a centimetre above the rib, behind the firing hand, so the
        // bead sits on the rib between the two hammers.
        if (id == "shotgun") return EyeStation({0.780f, 0.056f, 0.079f, 0.112f, -0.130f});
        // Two-handed, arms extended: the eye is well behind the slide.
        if (id == "pistol") return EyeStation({0.182f, 0.052f, 0.07875f, -0.0465f, -0.600f});
        // kit_nd_arsenal.scad: flip rear sight on the top-cover hinge, post on the gas block.
        // Same eye station as the AK-74 they are built on, so the dust cover stays low in the picture.
        if (id == "aks74u") return EyeStation({0.485f, 0.068f, 0.1075f, 0.175f, -0.165f});
        if (id == "bizon") return EyeStation({0.470f, 0.068f, 0.106f, 0.165f, -0.165f});
        if (id == "aps") return EyeStation({0.273f, 0.054f, 0.081f, -0.051f, -0.600f});
        if (id == "svt40") return EyeStation({0.805f, 0.066f, 0.091f, 0.150f, -0.240f});
        // Over-under: effects leave between the barrels, the bead rides the rib.
        if (id == "toz34") return EyeStation({0.785f, 0.048f, 0.087f, 0.115f, -0.130f});
        if (id == "knife") return {0.236f, 0.0f, 0.0f};
        if (id == "crowbar") return {0.510f, 0.0f, 0.0f};
        if (id == "axe") return {0.600f, 0.0f, 0.0f};
        return EyeStation({0.662f, 0.068f, 0.1078f, 0.160f, -0.165f}); // AK-74 notch / front post tip
    }

    inline int WeaponIndex(std::string_view id)
    {
        for (size_t i = 0; i < kWeapons.size(); ++i)
        {
            if (kWeapons[i].id == id)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
}
