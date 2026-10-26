#pragma once

// ============================================================================
// PatternJson.hpp - Reading an FPatternSpec out of JSON, in one place.
//
// Waves, ground targets and bosses all describe their guns with the same object,
// so they all have to accept the same field names and reject the same typos. A
// second copy of this reader is how "spinDegrees works on enemies but silently
// does nothing on bosses" happens.
// ============================================================================

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "Application/Game/NextRaiden/Combat/BulletPattern.hpp"

namespace NextRaiden
{
    /// Fills `spec` from `object`. Unknown fields and unknown enum names append a line to
    /// `outErrors` and make this return false; the caller decides whether that kills the
    /// whole entry or just that gun.
    bool ReadPatternSpec(const nlohmann::json& object, std::string_view context, FPatternSpec& spec,
                         std::vector<std::string>& outErrors);
}
