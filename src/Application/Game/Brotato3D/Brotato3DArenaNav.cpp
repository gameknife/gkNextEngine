#include "Brotato3DArenaNav.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace Brotato3D
{
    namespace
    {
        constexpr uint32_t orthogonalCost = 10;
        constexpr uint32_t diagonalCost = 14;
        constexpr uint32_t nearWallPenalty = 8;

        float Cross(const glm::vec2& a, const glm::vec2& b) { return a.x * b.y - a.y * b.x; }
    }

    void FArenaNav::Reset(const glm::vec2& halfExtent, float cellSize)
    {
        halfExtent_ = glm::max(halfExtent, glm::vec2(1.0f));
        cellSize_ = std::max(cellSize, 0.1f);
        width_ = static_cast<int>(std::ceil(halfExtent_.x * 2.0f / cellSize_));
        height_ = static_cast<int>(std::ceil(halfExtent_.y * 2.0f / cellSize_));
        blocked_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_), 0);
        flowBlocked_.clear();
        flowNearWall_.clear();
        flowDistance_.clear();
        flowSize_ = 0;
    }

    glm::ivec2 FArenaNav::WorldToCell(const glm::vec2& world) const
    {
        return glm::ivec2(static_cast<int>(std::floor((world.x + halfExtent_.x) / cellSize_)),
                          static_cast<int>(std::floor((world.y + halfExtent_.y) / cellSize_)));
    }

    glm::vec2 FArenaNav::CellCenter(int x, int z) const
    {
        return glm::vec2((static_cast<float>(x) + 0.5f) * cellSize_ - halfExtent_.x,
                         (static_cast<float>(z) + 0.5f) * cellSize_ - halfExtent_.y);
    }

    void FArenaNav::BlockSegment(const glm::vec2& a, const glm::vec2& b)
    {
        const float length = glm::length(b - a);
        const int steps = std::max(1, static_cast<int>(std::ceil(length / (cellSize_ * 0.5f))));
        for (int i = 0; i <= steps; ++i)
        {
            const glm::ivec2 cell = WorldToCell(glm::mix(a, b, static_cast<float>(i) / static_cast<float>(steps)));
            if (CellInBounds(cell.x, cell.y))
            {
                blocked_[cell.y * width_ + cell.x] = 1;
            }
        }
    }

    void FArenaNav::BlockTriangle(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c)
    {
        if (blocked_.empty())
        {
            return;
        }
        // Walls are vertical: their triangles project to lines, which only the edges catch.
        BlockSegment(a, b);
        BlockSegment(b, c);
        BlockSegment(c, a);
        const float area = Cross(b - a, c - a);
        if (std::abs(area) < 1.0e-4f)
        {
            return;
        }
        const glm::ivec2 lo = glm::max(WorldToCell(glm::min(a, glm::min(b, c))), glm::ivec2(0));
        const glm::ivec2 hi = glm::min(WorldToCell(glm::max(a, glm::max(b, c))), glm::ivec2(width_ - 1, height_ - 1));
        const float sign = area > 0.0f ? 1.0f : -1.0f;
        for (int z = lo.y; z <= hi.y; ++z)
        {
            for (int x = lo.x; x <= hi.x; ++x)
            {
                const glm::vec2 p = CellCenter(x, z);
                if (Cross(b - a, p - a) * sign >= 0.0f && Cross(c - b, p - b) * sign >= 0.0f &&
                    Cross(a - c, p - c) * sign >= 0.0f)
                {
                    blocked_[z * width_ + x] = 1;
                }
            }
        }
    }

    void FArenaNav::BlockBox(const glm::vec2& center, const glm::vec2& axisX, const glm::vec2& halfExtent)
    {
        const glm::vec2 ax = glm::length(axisX) > 1.0e-5f ? glm::normalize(axisX) : glm::vec2(1.0f, 0.0f);
        const glm::vec2 az(-ax.y, ax.x);
        const glm::vec2 ex = ax * halfExtent.x;
        const glm::vec2 ez = az * halfExtent.y;
        const glm::vec2 c0 = center - ex - ez;
        const glm::vec2 c1 = center + ex - ez;
        const glm::vec2 c2 = center + ex + ez;
        const glm::vec2 c3 = center - ex + ez;
        BlockTriangle(c0, c1, c2);
        BlockTriangle(c0, c2, c3);
    }

    void FArenaNav::FinishBuild(const glm::vec2& openPoint)
    {
        if (blocked_.empty())
        {
            return;
        }
        std::vector<uint8_t> reached(blocked_.size(), 0);
        std::vector<int> queue;
        queue.reserve(blocked_.size() / 4);
        const auto seed = [&](int x, int z)
        {
            const int index = z * width_ + x;
            if (CellInBounds(x, z) && !blocked_[index] && !reached[index])
            {
                reached[index] = 1;
                queue.push_back(index);
            }
        };
        for (int x = 0; x < width_; ++x)
        {
            seed(x, 0);
            seed(x, height_ - 1);
        }
        for (int z = 0; z < height_; ++z)
        {
            seed(0, z);
            seed(width_ - 1, z);
        }
        const glm::ivec2 open = WorldToCell(openPoint);
        seed(open.x, open.y);
        for (size_t head = 0; head < queue.size(); ++head)
        {
            const int x = queue[head] % width_;
            const int z = queue[head] / width_;
            seed(x + 1, z);
            seed(x - 1, z);
            seed(x, z + 1);
            seed(x, z - 1);
        }
        for (size_t index = 0; index < blocked_.size(); ++index)
        {
            if (!reached[index])
            {
                blocked_[index] = 1;
            }
        }
    }

    bool FArenaNav::IsBlocked(const glm::vec3& worldPos) const
    {
        if (blocked_.empty())
        {
            return false;
        }
        const glm::ivec2 cell = WorldToCell(glm::vec2(worldPos.x, worldPos.z));
        return CellInBounds(cell.x, cell.y) && blocked_[cell.y * width_ + cell.x] != 0;
    }

    float FArenaNav::BlockedFraction() const
    {
        if (blocked_.empty())
        {
            return 0.0f;
        }
        const size_t count = static_cast<size_t>(std::count(blocked_.begin(), blocked_.end(), uint8_t{1}));
        return static_cast<float>(count) / static_cast<float>(blocked_.size());
    }

    glm::vec3 FArenaNav::ResolveCircle(glm::vec3 pos, float radius) const
    {
        if (blocked_.empty())
        {
            return pos;
        }
        const float half = cellSize_ * 0.5f;
        for (int iteration = 0; iteration < 3; ++iteration)
        {
            bool moved = false;
            glm::vec2 p(pos.x, pos.z);
            const glm::ivec2 lo = WorldToCell(p - glm::vec2(radius));
            const glm::ivec2 hi = WorldToCell(p + glm::vec2(radius));
            for (int z = lo.y; z <= hi.y; ++z)
            {
                for (int x = lo.x; x <= hi.x; ++x)
                {
                    // Outside the grid is the arena wall's business, not this one's.
                    if (!CellInBounds(x, z) || !blocked_[z * width_ + x])
                    {
                        continue;
                    }
                    const glm::vec2 center = CellCenter(x, z);
                    const glm::vec2 closest = glm::clamp(p, center - glm::vec2(half), center + glm::vec2(half));
                    const glm::vec2 delta = p - closest;
                    const float distance = glm::length(delta);
                    if (distance >= radius || distance < 1.0e-5f)
                    {
                        continue;
                    }
                    p += delta / distance * (radius - distance);
                    moved = true;
                }
            }
            pos.x = p.x;
            pos.z = p.y;
            if (!moved)
            {
                break;
            }
        }
        if (IsBlocked(pos))
        {
            pos = FindNearestFree(pos, radius, 8.0f);
        }
        return pos;
    }

    glm::vec3 FArenaNav::FindNearestFree(const glm::vec3& pos, float clearance, float maxDistance) const
    {
        if (blocked_.empty())
        {
            return pos;
        }
        const glm::ivec2 origin = WorldToCell(glm::vec2(pos.x, pos.z));
        const int pad = std::max(0, static_cast<int>(std::ceil(clearance / cellSize_ - 0.5f)));
        const auto clear = [&](int x, int z)
        {
            for (int dz = -pad; dz <= pad; ++dz)
            {
                for (int dx = -pad; dx <= pad; ++dx)
                {
                    if (CellBlocked(x + dx, z + dz))
                    {
                        return false;
                    }
                }
            }
            return true;
        };
        const int maxRing = static_cast<int>(std::ceil(maxDistance / cellSize_));
        for (int ring = 0; ring <= maxRing; ++ring)
        {
            float bestDistance = 1.0e30f;
            glm::vec2 best(0.0f);
            for (int dz = -ring; dz <= ring; ++dz)
            {
                for (int dx = -ring; dx <= ring; ++dx)
                {
                    if (std::max(std::abs(dx), std::abs(dz)) != ring || !clear(origin.x + dx, origin.y + dz))
                    {
                        continue;
                    }
                    const glm::vec2 center = CellCenter(origin.x + dx, origin.y + dz);
                    const float distance = glm::length(center - glm::vec2(pos.x, pos.z));
                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        best = center;
                    }
                }
            }
            if (bestDistance < 1.0e29f)
            {
                return ring == 0 ? pos : glm::vec3(best.x, pos.y, best.y);
            }
        }
        return pos;
    }

    float FArenaNav::TraceSegment(const glm::vec3& from, const glm::vec3& to) const
    {
        if (blocked_.empty())
        {
            return 1.0f;
        }
        const glm::vec2 a(from.x, from.z);
        const glm::vec2 b(to.x, to.z);
        const float length = glm::length(b - a);
        const int steps = std::max(1, static_cast<int>(std::ceil(length / (cellSize_ * 0.5f))));
        for (int i = 1; i <= steps; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const glm::ivec2 cell = WorldToCell(glm::mix(a, b, t));
            if (CellInBounds(cell.x, cell.y) && blocked_[cell.y * width_ + cell.x])
            {
                return static_cast<float>(i - 1) / static_cast<float>(steps);
            }
        }
        return 1.0f;
    }

    bool FArenaNav::HasLineOfSight(const glm::vec3& from, const glm::vec3& to) const
    {
        return TraceSegment(from, to) >= 1.0f;
    }

    glm::ivec2 FArenaNav::WorldToFlowCell(const glm::vec2& world) const
    {
        return glm::ivec2(static_cast<int>(std::floor((world.x - flowOrigin_.x) / flowCellSize_)),
                          static_cast<int>(std::floor((world.y - flowOrigin_.y) / flowCellSize_)));
    }

    bool FArenaNav::FlowCellBlocked(int fx, int fz) const
    {
        return fx < 0 || fz < 0 || fx >= flowSize_ || fz >= flowSize_ || flowBlocked_[fz * flowSize_ + fx] != 0;
    }

    uint32_t FArenaNav::FlowAt(int fx, int fz) const
    {
        if (fx < 0 || fz < 0 || fx >= flowSize_ || fz >= flowSize_)
        {
            return unreachable;
        }
        return flowDistance_[fz * flowSize_ + fx];
    }

    void FArenaNav::UpdateFlowField(const glm::vec3& target, float windowHalf)
    {
        if (blocked_.empty())
        {
            return;
        }
        const int halfCells = std::max(4, static_cast<int>(std::ceil(windowHalf / flowCellSize_)));
        flowSize_ = halfCells * 2 + 1;
        const glm::vec2 targetXZ(target.x, target.z);
        // Snap the window to the flow lattice so cells keep their meaning frame to frame.
        flowOrigin_ = glm::floor(targetXZ / flowCellSize_) * flowCellSize_ - glm::vec2(static_cast<float>(halfCells) * flowCellSize_);
        const size_t count = static_cast<size_t>(flowSize_) * static_cast<size_t>(flowSize_);
        flowBlocked_.assign(count, 0);
        flowNearWall_.assign(count, 0);
        flowDistance_.assign(count, unreachable);

        // A flow cell is solid when any block cell it covers is (or it is off the arena).
        const int sub = std::max(1, static_cast<int>(std::ceil(flowCellSize_ / cellSize_)));
        for (int fz = 0; fz < flowSize_; ++fz)
        {
            for (int fx = 0; fx < flowSize_; ++fx)
            {
                const glm::vec2 cellMin = flowOrigin_ + glm::vec2(static_cast<float>(fx), static_cast<float>(fz)) * flowCellSize_;
                bool solid = false;
                for (int sz = 0; sz < sub && !solid; ++sz)
                {
                    for (int sx = 0; sx < sub && !solid; ++sx)
                    {
                        const glm::vec2 sample = cellMin + (glm::vec2(static_cast<float>(sx), static_cast<float>(sz)) + 0.5f) *
                                                               (flowCellSize_ / static_cast<float>(sub));
                        const glm::ivec2 cell = WorldToCell(sample);
                        solid = CellBlocked(cell.x, cell.y);
                    }
                }
                flowBlocked_[fz * flowSize_ + fx] = solid ? 1 : 0;
            }
        }
        for (int fz = 0; fz < flowSize_; ++fz)
        {
            for (int fx = 0; fx < flowSize_; ++fx)
            {
                bool wallNearby = false;
                for (int dz = -1; dz <= 1 && !wallNearby; ++dz)
                {
                    for (int dx = -1; dx <= 1 && !wallNearby; ++dx)
                    {
                        wallNearby = (dx != 0 || dz != 0) && FlowCellBlocked(fx + dx, fz + dz);
                    }
                }
                flowNearWall_[fz * flowSize_ + fx] = wallNearby ? 1 : 0;
            }
        }

        // Start from the target's cell, or the nearest open one if the target is in a wall.
        glm::ivec2 start = WorldToFlowCell(targetXZ);
        if (FlowCellBlocked(start.x, start.y))
        {
            bool found = false;
            for (int ring = 1; ring <= 4 && !found; ++ring)
            {
                for (int dz = -ring; dz <= ring && !found; ++dz)
                {
                    for (int dx = -ring; dx <= ring && !found; ++dx)
                    {
                        if (!FlowCellBlocked(start.x + dx, start.y + dz))
                        {
                            start += glm::ivec2(dx, dz);
                            found = true;
                        }
                    }
                }
            }
            if (!found)
            {
                return;
            }
        }

        using FEntry = std::pair<uint32_t, int>;
        std::priority_queue<FEntry, std::vector<FEntry>, std::greater<FEntry>> open;
        flowDistance_[start.y * flowSize_ + start.x] = 0;
        open.push({0u, start.y * flowSize_ + start.x});
        while (!open.empty())
        {
            const auto [distance, index] = open.top();
            open.pop();
            if (distance != flowDistance_[index])
            {
                continue;
            }
            const int x = index % flowSize_;
            const int z = index / flowSize_;
            for (int dz = -1; dz <= 1; ++dz)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if ((dx == 0 && dz == 0) || FlowCellBlocked(x + dx, z + dz))
                    {
                        continue;
                    }
                    const bool diagonal = dx != 0 && dz != 0;
                    // No corner cutting: a diagonal step needs both of its sides open.
                    if (diagonal && (FlowCellBlocked(x + dx, z) || FlowCellBlocked(x, z + dz)))
                    {
                        continue;
                    }
                    const int next = (z + dz) * flowSize_ + (x + dx);
                    const uint32_t cost = (diagonal ? diagonalCost : orthogonalCost) +
                                          (flowNearWall_[next] ? nearWallPenalty : 0u);
                    if (distance + cost < flowDistance_[next])
                    {
                        flowDistance_[next] = distance + cost;
                        open.push({distance + cost, next});
                    }
                }
            }
        }
    }

    bool FArenaNav::FlowDirection(const glm::vec3& from, glm::vec3& outDirection) const
    {
        if (flowDistance_.empty())
        {
            return false;
        }
        const glm::vec2 fromXZ(from.x, from.z);
        const glm::ivec2 cell = WorldToFlowCell(fromXZ);
        if (cell.x < 0 || cell.y < 0 || cell.x >= flowSize_ || cell.y >= flowSize_)
        {
            return false;
        }
        // A body pushed into a coarse solid cell still finds its way out through a neighbour.
        const bool here = !FlowCellBlocked(cell.x, cell.y);
        uint32_t best = here ? FlowAt(cell.x, cell.y) : unreachable;
        glm::ivec2 bestCell = cell;
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dz == 0)
                {
                    continue;
                }
                if (here && dx != 0 && dz != 0 &&
                    (FlowCellBlocked(cell.x + dx, cell.y) || FlowCellBlocked(cell.x, cell.y + dz)))
                {
                    continue;
                }
                const uint32_t distance = FlowAt(cell.x + dx, cell.y + dz);
                if (distance < best)
                {
                    best = distance;
                    bestCell = cell + glm::ivec2(dx, dz);
                }
            }
        }
        if (best == unreachable || bestCell == cell)
        {
            return false;
        }
        const glm::vec2 target = flowOrigin_ + (glm::vec2(bestCell) + 0.5f) * flowCellSize_;
        const glm::vec2 delta = target - fromXZ;
        const float length = glm::length(delta);
        if (length < 1.0e-4f)
        {
            return false;
        }
        outDirection = glm::vec3(delta.x / length, 0.0f, delta.y / length);
        return true;
    }
}
