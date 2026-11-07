#pragma once

#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"

namespace NextDayz
{
    enum class EAttachmentSlot : uint8_t { Optic, Grip, Magazine, Muzzle, Tactical, Count };
    enum class EWeaponAttachment : uint8_t
    {
        None, RedDot, Holographic, Scope4x, VerticalGrip, QuickMagazine, ExtendedMagazine, Suppressor, Flashlight, Count
    };

    inline constexpr size_t kAttachmentSlotCount = static_cast<size_t>(EAttachmentSlot::Count);
    using FWeaponAttachments = std::array<EWeaponAttachment, kAttachmentSlotCount>;

    struct FAttachmentDef
    {
        std::string_view id;
        std::string_view displayName;
        EAttachmentSlot slot;
        EWeaponAttachment type;
    };

    inline constexpr std::array<FAttachmentDef, 8> kAttachments = {{
        {"optic_red_dot", "Red Dot Sight", EAttachmentSlot::Optic, EWeaponAttachment::RedDot},
        {"optic_holographic", "Holographic Sight", EAttachmentSlot::Optic, EWeaponAttachment::Holographic},
        {"optic_4x", "4x Scope", EAttachmentSlot::Optic, EWeaponAttachment::Scope4x},
        {"grip_vertical", "Vertical Foregrip", EAttachmentSlot::Grip, EWeaponAttachment::VerticalGrip},
        {"mag_quick", "Quickdraw Magazine", EAttachmentSlot::Magazine, EWeaponAttachment::QuickMagazine},
        {"mag_extended", "Extended Magazine", EAttachmentSlot::Magazine, EWeaponAttachment::ExtendedMagazine},
        {"muzzle_suppressor", "Suppressor", EAttachmentSlot::Muzzle, EWeaponAttachment::Suppressor},
        {"tactical_flashlight", "Tactical Flashlight", EAttachmentSlot::Tactical, EWeaponAttachment::Flashlight},
    }};

    inline const FAttachmentDef* FindAttachmentDef(std::string_view id)
    {
        for (const auto& def : kAttachments) if (def.id == id) return &def;
        return nullptr;
    }

    inline const FAttachmentDef* FindAttachmentDef(EWeaponAttachment type)
    {
        for (const auto& def : kAttachments) if (def.type == type) return &def;
        return nullptr;
    }

    inline const char* AttachmentSlotName(EAttachmentSlot slot)
    {
        constexpr std::array names = {"Optic", "Foregrip", "Magazine", "Muzzle", "Tactical accessory"};
        return static_cast<size_t>(slot) < names.size() ? names[static_cast<size_t>(slot)] : "Unknown";
    }

    inline bool SupportsAttachment(std::string_view weaponId, EWeaponAttachment type)
    {
        const auto* weapon = FindWeaponDef(weaponId);
        const auto* attachment = FindAttachmentDef(type);
        if (!weapon || weapon->IsMelee() || !attachment) return false;
        switch (attachment->slot)
        {
        case EAttachmentSlot::Optic:
            if (weaponId == "mosin") return type == EWeaponAttachment::Scope4x;
            return type != EWeaponAttachment::Scope4x || (!weapon->IsSidearm() && weaponId != "shotgun");
        case EAttachmentSlot::Grip: return weaponId == "ak" || weaponId == "svd" || weaponId == "shotgun";
        case EAttachmentSlot::Magazine: return weaponId == "ak" || weaponId == "svd" || weapon->IsSidearm();
        case EAttachmentSlot::Muzzle:
        case EAttachmentSlot::Tactical: return true;
        default: return false;
        }
    }

    inline bool ValidAttachments(std::string_view weaponId, const FWeaponAttachments& attachments)
    {
        for (size_t slot = 0; slot < attachments.size(); ++slot)
        {
            if (attachments[slot] == EWeaponAttachment::None) continue;
            const auto* def = FindAttachmentDef(attachments[slot]);
            if (!def || static_cast<size_t>(def->slot) != slot || !SupportsAttachment(weaponId, def->type)) return false;
        }
        return true;
    }

    inline EWeaponAttachment AttachmentAt(const FWeaponAttachments& attachments, EAttachmentSlot slot)
    {
        return attachments[static_cast<size_t>(slot)];
    }

    inline int WeaponMagazineCapacity(std::string_view id, const FWeaponAttachments& attachments)
    {
        const auto* weapon = FindWeaponDef(id);
        if (!weapon) return 0;
        return AttachmentAt(attachments, EAttachmentSlot::Magazine) == EWeaponAttachment::ExtendedMagazine
            ? (weapon->magSize * 3 + 1) / 2 : weapon->magSize;
    }

    inline float WeaponReloadScale(const FWeaponAttachments& attachments)
    {
        return AttachmentAt(attachments, EAttachmentSlot::Magazine) == EWeaponAttachment::QuickMagazine ? 0.7f : 1.0f;
    }

    inline float WeaponStabilityScale(const FWeaponAttachments& attachments)
    {
        return AttachmentAt(attachments, EAttachmentSlot::Grip) == EWeaponAttachment::VerticalGrip ? 0.75f : 1.0f;
    }

    inline bool WeaponSuppressed(const FWeaponAttachments& attachments)
    {
        return AttachmentAt(attachments, EAttachmentSlot::Muzzle) == EWeaponAttachment::Suppressor;
    }

    // Rail locations in the held model's +Y-up / +Z-forward space.
    inline float WeaponOpticRailHeight(std::string_view id)
    {
        if (id == "pistol") return 0.081f;
        if (id == "shotgun") return 0.090f;
        if (id == "mosin" || id == "svd") return 0.106f;
        return 0.116f;
    }

    inline float WeaponOpticRailForward(std::string_view id)
    {
        if (id == "pistol") return 0.010f;
        if (id == "svd") return 0.050f;
        return 0.100f;
    }

    inline FWeaponModelPoints WeaponSightPoints(std::string_view id, const FWeaponAttachments& attachments)
    {
        auto points = WeaponModelPoints(id);
        const auto optic = AttachmentAt(attachments, EAttachmentSlot::Optic);
        if (optic == EWeaponAttachment::None) return points;
        const bool scope = optic == EWeaponAttachment::Scope4x;
        points.sightHeight = WeaponOpticRailHeight(id) + (scope ? 0.038f : optic == EWeaponAttachment::RedDot ? 0.035f : 0.045f);
        points.rearSightForward = WeaponOpticRailForward(id) - (scope ? 0.105f : 0.030f);
        // 1x optics keep the weapon's eye station (the head only rises to the
        // taller sight); a magnified optic puts the eye at the eyepiece.
        points.adsEyeRelief = scope ? 0.065f : (points.rearSightForward - points.eyeForward) * kViewModelScale;
        points.scopeFov = scope ? 22.0f : 0.0f;
        return points;
    }

    // First-person ADS FOV (vertical degrees): optics fix their own view, iron
    // sights only tighten the 75 degree view slightly.
    inline float WeaponAdsFov(std::string_view id, const FWeaponAttachments& attachments)
    {
        const auto points = WeaponSightPoints(id, attachments);
        if (points.scopeFov > 0.0f) return points.scopeFov;
        if (AttachmentAt(attachments, EAttachmentSlot::Optic) != EWeaponAttachment::None) return 62.0f;
        const auto* weapon = FindWeaponDef(id);
        return weapon ? weapon->adsFov : 75.0f;
    }

    // Seconds from hip to a settled sight picture: light guns snap up, long
    // rifles and magnified optics take longer (PUBG-like ADS-in times).
    inline float WeaponAimSeconds(std::string_view id, const FWeaponAttachments& attachments)
    {
        float seconds = 0.20f;
        if (id == "pistol") seconds = 0.15f;
        else if (id == "shotgun") seconds = 0.19f;
        else if (id == "mosin") seconds = 0.25f;
        else if (id == "svd") seconds = 0.26f;
        if (WeaponSightPoints(id, attachments).scopeFov > 0.0f) seconds += 0.04f;
        return seconds;
    }

    inline float SuppressorLength(std::string_view id)
    {
        return id == "pistol" ? 0.105f : 0.150f;
    }
}
