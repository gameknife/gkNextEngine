#include "Application/Game/NextRaiden/NextRaidenTypes.hpp"

#include <array>
#include <utility>

namespace NextRaiden
{
    namespace
    {
        template <typename TEnum, size_t N>
        bool Lookup(const std::array<std::pair<std::string_view, TEnum>, N>& table, std::string_view name, TEnum& out)
        {
            for (const auto& [key, value] : table)
            {
                if (key == name)
                {
                    out = value;
                    return true;
                }
            }
            return false;
        }

        constexpr std::array<std::pair<std::string_view, EBulletVisual>, 7> kBulletVisuals = {{
            {"bolt", EBulletVisual::Bolt},
            {"boltHostile", EBulletVisual::BoltHostile},
            {"orb", EBulletVisual::Orb},
            {"needle", EBulletVisual::Needle},
            {"missile", EBulletVisual::Missile},
            {"bomb", EBulletVisual::Bomb},
            {"laser", EBulletVisual::Laser},
        }};

        constexpr std::array<std::pair<std::string_view, EPatternKind>, 6> kPatterns = {{
            {"aimed", EPatternKind::Aimed},
            {"spread", EPatternKind::Spread},
            {"ring", EPatternKind::Ring},
            {"spiral", EPatternKind::Spiral},
            {"stream", EPatternKind::Stream},
            {"sweep", EPatternKind::Sweep},
        }};

        constexpr std::array<std::pair<std::string_view, EEnemyKind>, 3> kEnemies = {{
            {"scout", EEnemyKind::Scout},
            {"interceptor", EEnemyKind::Interceptor},
            {"bomber", EEnemyKind::Bomber},
        }};

        constexpr std::array<std::pair<std::string_view, EPathKind>, 6> kPaths = {{
            {"straight", EPathKind::Straight},
            {"sine", EPathKind::Sine},
            {"arcInOut", EPathKind::ArcInOut},
            {"dive", EPathKind::Dive},
            {"hover", EPathKind::Hover},
            {"strafe", EPathKind::Strafe},
        }};

        constexpr std::array<std::pair<std::string_view, EItemKind>, 6> kItems = {{
            {"none", EItemKind::None},   {"power", EItemKind::Power},   {"medal", EItemKind::Medal},
            {"repair", EItemKind::Repair}, {"bomb", EItemKind::Bomb},   {"option", EItemKind::Option},
        }};
    }

    bool ParseBulletVisual(std::string_view name, EBulletVisual& out) { return Lookup(kBulletVisuals, name, out); }
    bool ParsePatternKind(std::string_view name, EPatternKind& out) { return Lookup(kPatterns, name, out); }
    bool ParseEnemyKind(std::string_view name, EEnemyKind& out) { return Lookup(kEnemies, name, out); }
    bool ParsePathKind(std::string_view name, EPathKind& out) { return Lookup(kPaths, name, out); }
    bool ParseItemKind(std::string_view name, EItemKind& out) { return Lookup(kItems, name, out); }

    std::string_view EnemyKindName(EEnemyKind kind)
    {
        for (const auto& [key, value] : kEnemies)
        {
            if (value == kind)
            {
                return key;
            }
        }
        return "unknown";
    }
}
