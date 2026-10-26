#pragma once

// ============================================================================
// WaveDirector.hpp - The stage timeline: what shows up, when, in what shape.
//
// Parsing is strict. An unknown field or an unknown enum name is an error that
// names the offending wave and skips it, never a silent default - a typo'd
// pattern name would otherwise read in-game as "this wave just doesn't shoot",
// which is indistinguishable from a tuning decision.
// ============================================================================

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextRaiden/Combat/BulletPattern.hpp"
#include "Application/Game/NextRaiden/NextRaidenTypes.hpp"

namespace NextRaiden
{
    class FEnemyPool;
    class FScrollCamera;

    enum class EFormation : uint8_t
    {
        Line,    // abreast, across the screen
        Vee,     // a wedge, point first
        Column,  // nose to tail, arriving one after another
        Arc,     // shallow bow
        Count
    };

    bool ParseFormation(std::string_view name, EFormation& out);

    struct FWaveEvent
    {
        float Time = 0.0f;
        EEnemyKind Kind = EEnemyKind::Scout;
        EPathKind Path = EPathKind::Straight;
        EFormation Formation = EFormation::Line;
        int Count = 1;
        float X = 0.0f;           // formation centre
        float Spacing = 4.0f;
        float Speed = 0.0f;       // 0 = the kind's default
        float PathAmplitude = 6.0f;
        float PathPeriod = 2.4f;
        float HoverDepth = 12.0f;
        float HoverSeconds = 3.0f;
        bool CanFire = true;
        FPatternSpec Fire{};
        EItemKind Drop = EItemKind::None;
        /// Only the first aircraft of the formation carries the drop; a whole wave dropping
        /// five power-ups would end the difficulty curve on the spot.
        bool DropOnLeaderOnly = true;
    };

    class FWaveDirector
    {
    public:
        /// Returns false when the file is missing or unusable; `outErrors` collects one line
        /// per rejected wave either way.
        bool Load(const std::string& path, std::vector<std::string>& outErrors);
        void LoadEvents(std::vector<FWaveEvent> events);

        void Reset();
        /// Moves the cursor past everything due before `time` *without spawning any of it*.
        /// This is what makes "jump to the boss" usable: advancing the stage clock alone would
        /// release every wave up to that point in a single step.
        void SkipTo(float stageTime);
        /// Spawns everything whose time has come. Spawn depth is taken from the camera, so a
        /// wave always enters just beyond the top edge whatever the framing is.
        void Update(float stageTime, const FScrollCamera& camera, FEnemyPool& enemies);

        size_t EventCount() const { return events_.size(); }
        size_t Cursor() const { return cursor_; }
        const std::vector<FWaveEvent>& Events() const { return events_; }
        /// True once every wave in the table has been released.
        bool Exhausted() const { return cursor_ >= events_.size(); }

    private:
        std::vector<FWaveEvent> events_;
        size_t cursor_ = 0;
    };

    /// Formation offsets relative to the wave's centre, in metres. Exposed for the tests:
    /// formation shape is pure geometry and worth checking without a scene.
    void BuildFormation(EFormation formation, int count, float spacing, std::vector<glm::vec2>& out);
}
