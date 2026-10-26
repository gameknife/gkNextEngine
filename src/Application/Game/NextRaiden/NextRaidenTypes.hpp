#pragma once

// ============================================================================
// NextRaidenTypes.hpp - The small shared vocabulary: what kinds of bullet,
// enemy, path and item exist. Kept in one header so the config parser, the
// pools and the tests all spell them the same way.
// ============================================================================

#include <cstdint>
#include <string_view>

namespace NextRaiden
{
    /// One entry per bullet prefab in the rack. The visual is what decides which node pool a
    /// shot draws from, so it is part of the bullet's identity rather than a cosmetic flag.
    enum class EBulletVisual : uint8_t
    {
        Bolt,          // player vulcan
        BoltHostile,   // the same shape in enemy red
        Orb,           // big slow sphere
        Needle,        // fast thin dart
        Missile,
        Bomb,
        Laser,
        Count
    };

    enum class EPatternKind : uint8_t
    {
        Aimed,    // straight at the player
        Spread,   // fan centred on the aim direction
        Ring,     // full circle, optionally rotating volley to volley
        Spiral,   // continuous rotating arms
        Stream,   // aimed, fired as a short burst
        Sweep,    // one shot whose angle oscillates across the playfield
        Count
    };

    enum class EEnemyKind : uint8_t
    {
        Scout,
        Interceptor,
        Bomber,
        Count
    };

    enum class EPathKind : uint8_t
    {
        Straight,   // down the screen at a constant speed
        Sine,       // straight plus lateral oscillation
        ArcInOut,   // enters from a side, curves across, leaves
        Dive,       // accelerates towards the player's column
        Hover,      // stops at a depth, fires, then retreats
        Strafe,     // crosses horizontally without descending
        Count
    };

    enum class EItemKind : uint8_t
    {
        None,
        Power,
        Medal,
        Repair,
        Bomb,
        Option,
        Count
    };

    // Names as they appear in the JSON. Parsing is strict: an unknown name is an error the
    // loader reports rather than a silent fallback, because a typo'd pattern would otherwise
    // read as "this enemy simply never shoots".
    bool ParseBulletVisual(std::string_view name, EBulletVisual& out);
    bool ParsePatternKind(std::string_view name, EPatternKind& out);
    bool ParseEnemyKind(std::string_view name, EEnemyKind& out);
    bool ParsePathKind(std::string_view name, EPathKind& out);
    bool ParseItemKind(std::string_view name, EItemKind& out);

    std::string_view EnemyKindName(EEnemyKind kind);
}
