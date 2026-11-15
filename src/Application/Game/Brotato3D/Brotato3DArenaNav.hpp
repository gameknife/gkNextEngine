#pragma once

// ============================================================================
// Brotato3DArenaNav.hpp - What stands in the way on a Brotato3D arena, and how
// a horde gets around it. Pure logic: no scene, no renderer, unit-tested.
//
// Two grids over the arena rectangle (XZ, the gameplay plane):
//   * the BLOCK grid (0.5 m) says which ground is solid. It is filled by
//     rasterising triangles (the walls, posts and car bodies the arena system
//     extracts from the SCAD scene), then FinishBuild() flood-fills from the
//     arena edge and seals every pocket the flood cannot reach - the inside of
//     a building is four walls in the triangle data and solid afterwards.
//     Bodies are circles pushed out of solid cells; bullets and sight lines
//     stop at the first solid cell.
//   * the FLOW grid (1 m) is a Dijkstra distance field toward the player over
//     a window around them, rebuilt a few times a second. A zombie that cannot
//     see the player walks downhill on it, so the whole horde paths around
//     buildings for the price of one search. Cells next to a wall cost more,
//     which keeps the routes off the corners.
// ============================================================================

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace Brotato3D
{
    class FArenaNav
    {
    public:
        // Clears everything: an arena of [-halfExtent, +halfExtent] on X/Z.
        void Reset(const glm::vec2& halfExtent, float cellSize = 0.5f);

        // Building. Positions are world XZ (glm::vec2(x, z)).
        void BlockTriangle(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c);
        void BlockBox(const glm::vec2& center, const glm::vec2& axisX, const glm::vec2& halfExtent);
        // Seals every free pocket that is not connected to the arena edge or to `openPoint`
        // (the spawn: an arena ringed by buildings still has its middle).
        void FinishBuild(const glm::vec2& openPoint);

        bool IsBuilt() const { return !blocked_.empty(); }
        bool IsBlocked(const glm::vec3& worldPos) const;
        float BlockedFraction() const;

        // A circle of `radius` at `pos`, pushed out of every solid cell it overlaps.
        glm::vec3 ResolveCircle(glm::vec3 pos, float radius) const;
        // Nearest point whose cell is free and `clearance` away from solid ground, searching
        // rings out to `maxDistance`. Returns `pos` unchanged when nothing is found.
        glm::vec3 FindNearestFree(const glm::vec3& pos, float clearance = 0.5f, float maxDistance = 16.0f) const;
        bool HasLineOfSight(const glm::vec3& from, const glm::vec3& to) const;
        // Fraction [0, 1] of the segment travelled before the first solid cell (1 = clear).
        float TraceSegment(const glm::vec3& from, const glm::vec3& to) const;

        // Flow field toward `target`, covering a square of +-windowHalf metres around it.
        void UpdateFlowField(const glm::vec3& target, float windowHalf);
        // Downhill direction (XZ, normalised) from `from` toward the flow target. False when
        // `from` is outside the window or cannot reach the target.
        bool FlowDirection(const glm::vec3& from, glm::vec3& outDirection) const;
        bool HasFlowField() const { return !flowDistance_.empty(); }

    private:
        static constexpr uint32_t unreachable = 0xffffffffu;

        bool CellInBounds(int x, int z) const { return x >= 0 && z >= 0 && x < width_ && z < height_; }
        bool CellBlocked(int x, int z) const { return !CellInBounds(x, z) || blocked_[z * width_ + x] != 0; }
        glm::ivec2 WorldToCell(const glm::vec2& world) const;
        glm::vec2 CellCenter(int x, int z) const;
        void BlockSegment(const glm::vec2& a, const glm::vec2& b);
        bool FlowCellBlocked(int fx, int fz) const;
        glm::ivec2 WorldToFlowCell(const glm::vec2& world) const;
        uint32_t FlowAt(int fx, int fz) const;

        glm::vec2 halfExtent_{0.0f};
        float cellSize_ = 0.5f;
        int width_ = 0;
        int height_ = 0;
        std::vector<uint8_t> blocked_;

        // Flow field (cells of flowCellSize_ metres, window origin in world).
        float flowCellSize_ = 1.0f;
        glm::vec2 flowOrigin_{0.0f};
        int flowSize_ = 0;
        std::vector<uint8_t> flowBlocked_;
        std::vector<uint8_t> flowNearWall_;
        std::vector<uint32_t> flowDistance_;
    };
}
