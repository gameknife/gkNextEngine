#include "Gameplay/AI/NavGrid.h"
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

namespace
{
    constexpr int kDirX[] = {1, -1, 0, 0, 1, -1, 1, -1};
    constexpr int kDirZ[] = {0, 0, 1, -1, 1, -1, -1, 1};
    constexpr float kDiagonalInvSqrt = 0.70710678f;
}

namespace NextGameplay
{
void FNavGrid::Build(Assets::CPU::FCPUAccelerationStructure& bvh, const FNavGridSettings& settings)
{
    sampledCellCount_ = 0;
    geometrySampledCellCount_ = 0;
    settings_ = settings;
    width_ = static_cast<int>(std::ceil((settings_.worldMax.x - settings_.worldMin.x) / settings_.cellSize));
    height_ = static_cast<int>(std::ceil((settings_.worldMax.z - settings_.worldMin.z) / settings_.cellSize));

    if (width_ <= 0 || height_ <= 0)
    {
        cells_.clear();
        spdlog::warn("NavGrid::Build - invalid bounds, grid not built");
        return;
    }

    cells_.assign(width_ * height_, {});

    const FGridRect fullRect{0, 0, width_ - 1, height_ - 1};
    SampleBaseWalkability(bvh, fullRect);
    UpdateWalkabilityFromBase(fullRect);

    int walkableCount = 0;
    for (const FNavCell& cell : cells_)
    {
        if (cell.walkable)
        {
            ++walkableCount;
        }
    }

    spdlog::info("NavGrid built: {}x{} cells, {} walkable, cellSize={:.2f}m, agentRadius={:.2f}m",
                 width_, height_, walkableCount, settings_.cellSize, settings_.agentRadius);
}

size_t FNavGrid::BuildShifted(Assets::CPU::FCPUAccelerationStructure& bvh, const FNavGridSettings& settings,
                             const FNavGrid& previous)
{
    const glm::vec3 offset = (settings.worldMin - previous.settings_.worldMin) / settings.cellSize;
    const int shiftX = static_cast<int>(std::round(offset.x));
    const int shiftZ = static_cast<int>(std::round(offset.z));
    const auto& old = previous.settings_;
    const int width = static_cast<int>(std::ceil((settings.worldMax.x - settings.worldMin.x) / settings.cellSize));
    const int height = static_cast<int>(std::ceil((settings.worldMax.z - settings.worldMin.z) / settings.cellSize));
    const bool compatible = this != &previous && previous.IsBuilt() && width == previous.width_ && height == previous.height_ &&
        settings.cellSize == old.cellSize && settings.agentRadius == old.agentRadius &&
        settings.maxSlopeAngle == old.maxSlopeAngle && settings.clearanceHeight == old.clearanceHeight &&
        settings.maxStepHeight == old.maxStepHeight && settings.floorSearchHeight == old.floorSearchHeight &&
        static_cast<bool>(settings.sampleFloorHeight) == static_cast<bool>(old.sampleFloorHeight) &&
        static_cast<bool>(settings.isUnwalkable) == static_cast<bool>(old.isUnwalkable) &&
        settings.terrain == old.terrain && settings.terrainInstanceIds == old.terrainInstanceIds &&
        (settings.sampleFloorHeight || settings.sampleCeiling == old.sampleCeiling) &&
        std::abs(offset.x - shiftX) < 0.001f && std::abs(offset.z - shiftZ) < 0.001f &&
        std::abs(shiftX) < width && std::abs(shiftZ) < height;
    if (!compatible)
    {
        Build(bvh, settings);
        return sampledCellCount_;
    }

    settings_ = settings;
    width_ = width;
    height_ = height;
    sampledCellCount_ = 0;
    geometrySampledCellCount_ = 0;
    cells_.resize(static_cast<size_t>(width_) * height_);
    const FGridRect overlap{std::max(0, -shiftX), std::max(0, -shiftZ),
                            std::min(width_ - 1, width_ - 1 - shiftX),
                            std::min(height_ - 1, height_ - 1 - shiftZ)};
    for (int gz = overlap.minGz; gz <= overlap.maxGz; ++gz)
    {
        std::copy_n(previous.cells_.begin() + previous.CellIndex(overlap.minGx + shiftX, gz + shiftZ),
                    overlap.maxGx - overlap.minGx + 1, cells_.begin() + CellIndex(overlap.minGx, gz));
    }
    // Non-overlapping strips cover each entering cell once, including diagonal
    // moves. Re-derive walkability so erosion at the old boundary is repaired.
    for (const FGridRect rect : {FGridRect{0, 0, width_ - 1, overlap.minGz - 1},
                                FGridRect{0, overlap.maxGz + 1, width_ - 1, height_ - 1},
                                FGridRect{0, overlap.minGz, overlap.minGx - 1, overlap.maxGz},
                                FGridRect{overlap.maxGx + 1, overlap.minGz, width_ - 1, overlap.maxGz}})
    {
        SampleBaseWalkability(bvh, rect);
    }
    UpdateWalkabilityFromBase({0, 0, width_ - 1, height_ - 1});
    return sampledCellCount_;
}

void FNavGrid::MaskUnwalkable(const std::function<bool(const glm::vec3&)>& predicate)
{
    if (cells_.empty() || !predicate)
    {
        return;
    }
    int maskedCount = 0;
    for (int gz = 0; gz < height_; ++gz)
    {
        for (int gx = 0; gx < width_; ++gx)
        {
            FNavCell& cell = cells_[CellIndex(gx, gz)];
            if (cell.baseWalkable && predicate(GetCellWorldPosition(gx, gz)))
            {
                cell.baseWalkable = false;
                ++maskedCount;
            }
        }
    }
    // Re-derive walkable from base so agent-radius erosion respects the mask.
    const FGridRect fullRect{0, 0, width_ - 1, height_ - 1};
    UpdateWalkabilityFromBase(fullRect);
    spdlog::info("NavGrid mask: {} cells vetoed", maskedCount);
}

void FNavGrid::RebuildDirtyRegion(Assets::CPU::FCPUAccelerationStructure& bvh, const glm::vec3& dirtyWorldMin,
                                  const glm::vec3& dirtyWorldMax)
{
    if (cells_.empty() || width_ <= 0 || height_ <= 0)
    {
        return;
    }

    const FGridRect dirtyRect = MakeGridRectFromWorldBounds(dirtyWorldMin, dirtyWorldMax);
    if (!IsGridRectValid(dirtyRect))
    {
        return;
    }

    const FGridRect changedBaseRect = ExpandGridRect(dirtyRect, GetFootprintMarginCells());
    if (!IsGridRectValid(changedBaseRect))
    {
        return;
    }

    SampleBaseWalkability(bvh, changedBaseRect);

    const FGridRect walkableRect = ExpandGridRect(changedBaseRect, GetExtraErosionCells());
    UpdateWalkabilityFromBase(walkableRect);

    spdlog::info("NavGrid partial rebuild: worldMin=({:.2f}, {:.2f}, {:.2f}) worldMax=({:.2f}, {:.2f}, {:.2f}) grid=[{},{}]-[{},{}]",
                 dirtyWorldMin.x, dirtyWorldMin.y, dirtyWorldMin.z,
                 dirtyWorldMax.x, dirtyWorldMax.y, dirtyWorldMax.z,
                 walkableRect.minGx, walkableRect.minGz, walkableRect.maxGx, walkableRect.maxGz);
}

std::vector<glm::vec3> FNavGrid::FindPath(const glm::vec3& from, const glm::vec3& to, float referenceHeight,
                                          int maxExpansions) const
{
    if (cells_.empty())
    {
        return {};
    }

    glm::ivec2 startGrid = WorldToGrid(from);
    glm::ivec2 goalGrid = WorldToGrid(to);

    if (!InBounds(startGrid.x, startGrid.y) ||
        !cells_[CellIndex(startGrid.x, startGrid.y)].walkable)
    {
        startGrid = FindNearestWalkable(startGrid, referenceHeight);
    }
    if (!InBounds(goalGrid.x, goalGrid.y) ||
        !cells_[CellIndex(goalGrid.x, goalGrid.y)].walkable)
    {
        goalGrid = FindNearestWalkable(goalGrid, to.y);
    }

    if (startGrid.x < 0 || goalGrid.x < 0)
    {
        return {};
    }

    if (startGrid == goalGrid)
    {
        return {GridToWorld(startGrid.x, startGrid.y)};
    }

    // Scratch reused across searches on this thread: a 1 km grid is ~260k
    // cells, too much to allocate and clear per query. Only the cells the
    // previous search touched are reset.
    const int totalCells = width_ * height_;
    thread_local std::vector<float> gCost;
    thread_local std::vector<int> parent;
    thread_local std::vector<int> touched;
    if (static_cast<int>(gCost.size()) != totalCells)
    {
        gCost.assign(totalCells, std::numeric_limits<float>::max());
        parent.assign(totalCells, -1);
        touched.clear();
    }
    for (const int idx : touched)
    {
        gCost[idx] = std::numeric_limits<float>::max();
        parent[idx] = -1;
    }
    touched.clear();

    struct FOpenNode
    {
        float fCost;
        int index;
        bool operator>(const FOpenNode& other) const { return fCost > other.fCost; }
    };

    std::priority_queue<FOpenNode, std::vector<FOpenNode>, std::greater<FOpenNode>> openList;

    const int startIdx = CellIndex(startGrid.x, startGrid.y);
    const int goalIdx = CellIndex(goalGrid.x, goalGrid.y);
    const glm::vec3 goalWorld = GridToWorld(goalGrid.x, goalGrid.y);
    gCost[startIdx] = 0.0f;
    touched.push_back(startIdx);
    openList.push({glm::distance(GridToWorld(startGrid.x, startGrid.y), goalWorld), startIdx});

    bool found = false;
    int expansions = 0;
    int closestIdx = startIdx;
    float closestHeuristic = std::numeric_limits<float>::max();

    while (!openList.empty())
    {
        const FOpenNode current = openList.top();
        openList.pop();

        if (current.index == goalIdx)
        {
            found = true;
            break;
        }

        const int cx = current.index % width_;
        const int cz = current.index / width_;
        const float currentHeuristic = glm::distance(GridToWorld(cx, cz), goalWorld);
        if (current.fCost > gCost[current.index] + currentHeuristic + 0.001f)
        {
            continue;
        }
        if (currentHeuristic < closestHeuristic)
        {
            closestHeuristic = currentHeuristic;
            closestIdx = current.index;
        }
        if (maxExpansions > 0 && ++expansions > maxExpansions)
        {
            break;
        }

        for (int d = 0; d < 8; ++d)
        {
            const int nx = cx + kDirX[d];
            const int nz = cz + kDirZ[d];

            if (!InBounds(nx, nz))
            {
                continue;
            }

            const int nIdx = CellIndex(nx, nz);
            if (!cells_[nIdx].walkable || !CanTraverseByIndex(current.index, nIdx))
            {
                continue;
            }

            // Prevent diagonal corner cutting
            if (d >= 4)
            {
                const int adj1x = cx + kDirX[d];
                const int adj1z = cz;
                const int adj2x = cx;
                const int adj2z = cz + kDirZ[d];
                if (!CanTraverse(cx, cz, adj1x, adj1z) ||
                    !CanTraverse(cx, cz, adj2x, adj2z) ||
                    !CanTraverse(adj1x, adj1z, nx, nz) ||
                    !CanTraverse(adj2x, adj2z, nx, nz))
                {
                    continue;
                }
            }

            const float moveCost = glm::distance(GridToWorld(cx, cz), GridToWorld(nx, nz));
            const float newG = gCost[current.index] + moveCost;

            if (newG < gCost[nIdx])
            {
                if (gCost[nIdx] == std::numeric_limits<float>::max())
                {
                    touched.push_back(nIdx);
                }
                gCost[nIdx] = newG;
                parent[nIdx] = current.index;
                const float h = glm::distance(GridToWorld(nx, nz), goalWorld);
                openList.push({newG + h, nIdx});
            }
        }
    }

    int endIdx = goalIdx;
    if (!found)
    {
        if (maxExpansions <= 0 || closestIdx == startIdx)
        {
            return {};
        }
        endIdx = closestIdx;   // bounded search: get as close as the budget allowed
    }

    // Reconstruct grid path
    std::vector<glm::ivec2> gridPath;
    for (int idx = endIdx; idx != -1; idx = parent[idx])
    {
        gridPath.push_back({idx % width_, idx / width_});
    }
    std::reverse(gridPath.begin(), gridPath.end());

    // Smooth path
    SmoothPath(gridPath);

    // Convert to world positions
    std::vector<glm::vec3> worldPath;
    worldPath.reserve(gridPath.size());
    for (const auto& gp : gridPath)
    {
        worldPath.push_back(GridToWorld(gp.x, gp.y));
    }

    return worldPath;
}

bool FNavGrid::IsWalkable(const glm::vec3& worldPos) const
{
    const glm::ivec2 g = WorldToGrid(worldPos);
    if (!InBounds(g.x, g.y))
    {
        return false;
    }
    return cells_[CellIndex(g.x, g.y)].walkable;
}

bool FNavGrid::SampleGroundHeight(const glm::vec3& worldPos, float& outHeight) const
{
    const glm::ivec2 g = WorldToGrid(worldPos);
    if (!InBounds(g.x, g.y))
    {
        return false;
    }
    const FNavCell& cell = cells_[CellIndex(g.x, g.y)];
    if (!cell.baseWalkable)
    {
        return false;
    }
    outHeight = cell.groundHeight;
    return true;
}

bool FNavGrid::IsCellWalkable(int gx, int gz) const
{
    return InBounds(gx, gz) && cells_[CellIndex(gx, gz)].walkable;
}

bool FNavGrid::IsCellReachable(int gx, int gz, float referenceHeight) const
{
    if (!InBounds(gx, gz) || !cells_[CellIndex(gx, gz)].walkable)
    {
        return false;
    }
    return std::abs(cells_[CellIndex(gx, gz)].groundHeight - referenceHeight) <= settings_.floorHeightTolerance;
}

glm::vec3 FNavGrid::GetCellWorldPosition(int gx, int gz) const
{
    return GridToWorld(gx, gz);
}

glm::ivec2 FNavGrid::WorldToGrid(const glm::vec3& worldPos) const
{
    return {
        static_cast<int>(std::floor((worldPos.x - settings_.worldMin.x) / settings_.cellSize)),
        static_cast<int>(std::floor((worldPos.z - settings_.worldMin.z) / settings_.cellSize))
    };
}

glm::vec3 FNavGrid::GridToWorld(int gx, int gz) const
{
    const float x = settings_.worldMin.x + (static_cast<float>(gx) + 0.5f) * settings_.cellSize;
    const float z = settings_.worldMin.z + (static_cast<float>(gz) + 0.5f) * settings_.cellSize;
    const float y = cells_[CellIndex(gx, gz)].groundHeight;
    return {x, y, z};
}

int FNavGrid::CellIndex(int gx, int gz) const
{
    return gz * width_ + gx;
}

bool FNavGrid::InBounds(int gx, int gz) const
{
    return gx >= 0 && gx < width_ && gz >= 0 && gz < height_;
}

glm::ivec2 FNavGrid::FindNearestWalkable(const glm::ivec2& cell, float referenceHeight) const
{
    constexpr int kMaxRadius = 5;
    float bestScore = std::numeric_limits<float>::max();
    glm::ivec2 bestCell(-1, -1);

    for (int r = 0; r <= kMaxRadius; ++r)
    {
        for (int dz = -r; dz <= r; ++dz)
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                if (std::abs(dx) != r && std::abs(dz) != r)
                {
                    continue;
                }
                int nx = cell.x + dx;
                int nz = cell.y + dz;
                if (!InBounds(nx, nz))
                {
                    continue;
                }

                const FNavCell& candidate = cells_[CellIndex(nx, nz)];
                if (!candidate.walkable)
                {
                    continue;
                }

                const float horizontalDistance = glm::length(glm::vec2(static_cast<float>(dx), static_cast<float>(dz)));
                const float heightPenalty = std::abs(candidate.groundHeight - referenceHeight) /
                                            std::max(settings_.maxStepHeight, 0.05f);
                const float score = horizontalDistance + heightPenalty * 0.35f;
                if (score < bestScore)
                {
                    bestScore = score;
                    bestCell = {nx, nz};
                }
            }
        }

        if (bestCell.x >= 0)
        {
            return bestCell;
        }
    }

    return {-1, -1};
}

void FNavGrid::SmoothPath(std::vector<glm::ivec2>& gridPath) const
{
    if (gridPath.size() <= 2)
    {
        return;
    }

    std::vector<glm::ivec2> smoothed;
    smoothed.push_back(gridPath.front());

    size_t current = 0;
    while (current < gridPath.size() - 1)
    {
        size_t farthest = current + 1;
        for (size_t j = gridPath.size() - 1; j > current + 1; --j)
        {
            if (GridLineWalkable(gridPath[current], gridPath[j]))
            {
                farthest = j;
                break;
            }
        }
        smoothed.push_back(gridPath[farthest]);
        current = farthest;
    }

    gridPath = std::move(smoothed);
}

bool FNavGrid::GridLineWalkable(const glm::ivec2& a, const glm::ivec2& b) const
{
    // Bresenham line check
    int x0 = a.x, z0 = a.y;
    int x1 = b.x, z1 = b.y;
    int dx = std::abs(x1 - x0);
    int dz = std::abs(z1 - z0);
    int sx = (x0 < x1) ? 1 : -1;
    int sz = (z0 < z1) ? 1 : -1;
    int err = dx - dz;
    int previousX = x0;
    int previousZ = z0;
    bool hasPrevious = false;

    while (true)
    {
        if (!InBounds(x0, z0) || !cells_[CellIndex(x0, z0)].walkable)
        {
            return false;
        }

        if (hasPrevious && !CanTraverse(previousX, previousZ, x0, z0))
        {
            return false;
        }

        if (x0 == x1 && z0 == z1)
        {
            break;
        }

        previousX = x0;
        previousZ = z0;
        hasPrevious = true;

        int e2 = 2 * err;
        if (e2 > -dz)
        {
            err -= dz;
            x0 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            z0 += sz;
        }
    }
    return true;
}

std::vector<uint8_t> FNavGrid::BuildReachabilityMask(const glm::vec3& from, float referenceHeight) const
{
    std::vector<uint8_t> mask(cells_.size(), 0);
    if (cells_.empty())
    {
        return mask;
    }

    glm::ivec2 startGrid = WorldToGrid(from);
    if (!InBounds(startGrid.x, startGrid.y) ||
        !cells_[CellIndex(startGrid.x, startGrid.y)].walkable)
    {
        startGrid = FindNearestWalkable(startGrid, referenceHeight);
    }

    if (startGrid.x < 0)
    {
        return mask;
    }

    std::queue<int> open;
    const int startIdx = CellIndex(startGrid.x, startGrid.y);
    mask[startIdx] = 1;
    open.push(startIdx);

    while (!open.empty())
    {
        const int currentIdx = open.front();
        open.pop();

        const int cx = currentIdx % width_;
        const int cz = currentIdx / width_;
        for (int d = 0; d < 8; ++d)
        {
            const int nx = cx + kDirX[d];
            const int nz = cz + kDirZ[d];
            if (!InBounds(nx, nz))
            {
                continue;
            }

            const int nextIdx = CellIndex(nx, nz);
            if (mask[nextIdx] != 0 || !cells_[nextIdx].walkable || !CanTraverseByIndex(currentIdx, nextIdx))
            {
                continue;
            }

            if (d >= 4)
            {
                const int adj1x = cx + kDirX[d];
                const int adj1z = cz;
                const int adj2x = cx;
                const int adj2z = cz + kDirZ[d];
                if (!CanTraverse(cx, cz, adj1x, adj1z) ||
                    !CanTraverse(cx, cz, adj2x, adj2z) ||
                    !CanTraverse(adj1x, adj1z, nx, nz) ||
                    !CanTraverse(adj2x, adj2z, nx, nz))
                {
                    continue;
                }
            }

            mask[nextIdx] = 1;
            open.push(nextIdx);
        }
    }

    return mask;
}

bool FNavGrid::CanTraverse(int fromGx, int fromGz, int toGx, int toGz) const
{
    if (!InBounds(fromGx, fromGz) || !InBounds(toGx, toGz))
    {
        return false;
    }
    return CanTraverseByIndex(CellIndex(fromGx, fromGz), CellIndex(toGx, toGz));
}

bool FNavGrid::CanTraverseByIndex(int fromIdx, int toIdx) const
{
    if (fromIdx < 0 || toIdx < 0 ||
        fromIdx >= static_cast<int>(cells_.size()) || toIdx >= static_cast<int>(cells_.size()))
    {
        return false;
    }

    const FNavCell& fromCell = cells_[fromIdx];
    const FNavCell& toCell = cells_[toIdx];
    if (!fromCell.walkable || !toCell.walkable)
    {
        return false;
    }

    return std::abs(toCell.groundHeight - fromCell.groundHeight) <= settings_.maxStepHeight;
}

FNavGrid::FGridRect FNavGrid::MakeGridRectFromWorldBounds(const glm::vec3& worldMin, const glm::vec3& worldMax) const
{
    if (cells_.empty())
    {
        return {};
    }

    const glm::vec3 clampedMin(
        glm::clamp(std::min(worldMin.x, worldMax.x), settings_.worldMin.x, settings_.worldMax.x),
        0.0f,
        glm::clamp(std::min(worldMin.z, worldMax.z), settings_.worldMin.z, settings_.worldMax.z));
    const glm::vec3 clampedMax(
        glm::clamp(std::max(worldMin.x, worldMax.x), settings_.worldMin.x, settings_.worldMax.x),
        0.0f,
        glm::clamp(std::max(worldMin.z, worldMax.z), settings_.worldMin.z, settings_.worldMax.z));

    const int minGx =
        static_cast<int>(std::floor((clampedMin.x - settings_.worldMin.x) / settings_.cellSize));
    const int minGz =
        static_cast<int>(std::floor((clampedMin.z - settings_.worldMin.z) / settings_.cellSize));
    const int maxGx =
        static_cast<int>(std::floor((clampedMax.x - settings_.worldMin.x) / settings_.cellSize));
    const int maxGz =
        static_cast<int>(std::floor((clampedMax.z - settings_.worldMin.z) / settings_.cellSize));

    return ExpandGridRect({minGx, minGz, maxGx, maxGz}, 0);
}

FNavGrid::FGridRect FNavGrid::ExpandGridRect(const FGridRect& rect, int margin) const
{
    if (width_ <= 0 || height_ <= 0)
    {
        return {};
    }

    FGridRect expanded;
    expanded.minGx = glm::clamp(rect.minGx - margin, 0, width_ - 1);
    expanded.minGz = glm::clamp(rect.minGz - margin, 0, height_ - 1);
    expanded.maxGx = glm::clamp(rect.maxGx + margin, 0, width_ - 1);
    expanded.maxGz = glm::clamp(rect.maxGz + margin, 0, height_ - 1);
    return expanded;
}

bool FNavGrid::IsGridRectValid(const FGridRect& rect) const
{
    return rect.minGx <= rect.maxGx && rect.minGz <= rect.maxGz;
}

void FNavGrid::SampleBaseWalkability(Assets::CPU::FCPUAccelerationStructure& bvh, const FGridRect& rect)
{
    if (!IsGridRectValid(rect))
    {
        return;
    }
    sampledCellCount_ += static_cast<size_t>(rect.maxGx - rect.minGx + 1) * (rect.maxGz - rect.minGz + 1);

    // Every cell is an independent set of rays against the immutable BVH
    // snapshot, so rows are sampled in parallel; a 1 km map is ~260k cells.
    const auto snapshot = bvh.AcquireSnapshot();
    const int rectWidth = rect.maxGx - rect.minGx + 1;
    std::vector<uint8_t> detailed;
    if (snapshot && settings_.terrain && settings_.terrain->HasData() && !settings_.terrainInstanceIds.empty())
    {
        detailed.assign(static_cast<size_t>(rectWidth) * (rect.maxGz - rect.minGz + 1), 0);
        // Conservative broad phase: include every non-terrain, ray-visible
        // instance's complete bounds plus the footprint. Roofs, bridges,
        // interiors and even thin/small props retain the full geometry path.
        const glm::vec3 margin(settings_.agentRadius + settings_.cellSize);
        for (const auto& instance : snapshot->contexts)
        {
            if (!instance.rayCastVisible || std::find(settings_.terrainInstanceIds.begin(),
                settings_.terrainInstanceIds.end(), instance.nodeId) != settings_.terrainInstanceIds.end()) continue;
            const auto low = WorldToGrid(instance.worldBoundsMin - margin);
            const auto high = WorldToGrid(instance.worldBoundsMax + margin);
            const int minX = std::max(rect.minGx, low.x);
            const int maxX = std::min(rect.maxGx, high.x);
            const int minZ = std::max(rect.minGz, low.y);
            const int maxZ = std::min(rect.maxGz, high.y);
            if (minX > maxX) continue;
            for (int z = minZ; z <= maxZ; ++z)
            {
                std::fill_n(detailed.begin() + (z - rect.minGz) * rectWidth + minX - rect.minGx,
                            maxX - minX + 1, uint8_t{1});
            }
        }
    }
    geometrySampledCellCount_ += detailed.empty()
        ? static_cast<size_t>(rectWidth) * (rect.maxGz - rect.minGz + 1)
        : static_cast<size_t>(std::count(detailed.begin(), detailed.end(), uint8_t{1}));
    const auto sampleRows = [this, &snapshot, &rect, &detailed, rectWidth](int firstRow, int rowStep)
    {
        for (int gz = rect.minGz + firstRow; gz <= rect.maxGz; gz += rowStep)
        {
            for (int gx = rect.minGx; gx <= rect.maxGx; ++gx)
            {
                FNavCell sampledCell;
                if (!detailed.empty() && detailed[(gz - rect.minGz) * rectWidth + gx - rect.minGx] == 0)
                {
                    SampleTerrainCell(gx, gz, sampledCell);
                }
                else if (snapshot)
                {
                    SampleBaseCell(*snapshot, gx, gz, sampledCell);
                }
                cells_[CellIndex(gx, gz)] = sampledCell;
            }
        }
    };
    const int rows = rect.maxGz - rect.minGz + 1;
    const int workers = std::max(1, std::min({static_cast<int>(std::thread::hardware_concurrency()),
                                             rows / 8 + 1, settings_.maxSamplingWorkers}));
    if (workers <= 1)
    {
        sampleRows(0, 1);
        return;
    }
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(workers - 1));
    for (int worker = 1; worker < workers; ++worker)
    {
        threads.emplace_back(sampleRows, worker, workers);
    }
    sampleRows(0, workers);
    for (std::thread& thread : threads)
    {
        thread.join();
    }
}

void FNavGrid::UpdateWalkabilityFromBase(const FGridRect& rect)
{
    if (!IsGridRectValid(rect))
    {
        return;
    }

    const int erosionCells = GetExtraErosionCells();
    for (int gz = rect.minGz; gz <= rect.maxGz; ++gz)
    {
        for (int gx = rect.minGx; gx <= rect.maxGx; ++gx)
        {
            FNavCell& cell = cells_[CellIndex(gx, gz)];
            cell.walkable = cell.baseWalkable;
            if (!cell.baseWalkable)
            {
                continue;
            }

            bool blockedByNeighbor = false;
            for (int dz = -erosionCells; dz <= erosionCells && !blockedByNeighbor; ++dz)
            {
                for (int dx = -erosionCells; dx <= erosionCells; ++dx)
                {
                    if (dx == 0 && dz == 0)
                    {
                        continue;
                    }

                    const int nx = gx + dx;
                    const int nz = gz + dz;
                    if (!InBounds(nx, nz) || !cells_[CellIndex(nx, nz)].baseWalkable)
                    {
                        blockedByNeighbor = true;
                        break;
                    }
                }
            }

            if (blockedByNeighbor)
            {
                cell.walkable = false;
            }
        }
    }
}

bool FNavGrid::SampleTerrainCell(int gx, int gz, FNavCell& outCell) const
{
    outCell = {};
    const auto& terrain = *settings_.terrain;
    const auto& data = *terrain.GetGridData();
    const auto contains = [&data](float x, float z)
    {
        // ContainsWorld intentionally includes half a terrain cell of seam
        // slack. Navigation must not invent ground beyond the actual mesh.
        const glm::dvec4 local = data.worldToLocal * glm::dvec4(x, 0.0, z, 1.0);
        return std::abs(local.x) <= data.sizeX * 0.5 && std::abs(local.y) <= data.sizeY * 0.5;
    };
    const glm::vec3 centre = GridToWorld(gx, gz);
    if (!contains(centre.x, centre.z)) return false;
    const float ground = terrain.SampleHeight(centre.x, centre.z);
    outCell.groundHeight = ground;
    if (settings_.isUnwalkable && settings_.isUnwalkable({centre.x, ground, centre.z})) return false;
    const float slopeCos = std::cos(glm::radians(settings_.maxSlopeAngle));
    if (terrain.SampleNormal(centre.x, centre.z).y < slopeCos) return false;
    if (settings_.agentRadius > 0.001f)
    {
        for (const glm::vec2 offset : {glm::vec2(1, 0), {-1, 0}, {0, 1}, {0, -1},
                                      {kDiagonalInvSqrt, kDiagonalInvSqrt}, {-kDiagonalInvSqrt, kDiagonalInvSqrt},
                                      {kDiagonalInvSqrt, -kDiagonalInvSqrt}, {-kDiagonalInvSqrt, -kDiagonalInvSqrt}})
        {
            const float x = centre.x + offset.x * settings_.agentRadius;
            const float z = centre.z + offset.y * settings_.agentRadius;
            if (!contains(x, z) || std::abs(terrain.SampleHeight(x, z) - ground) > settings_.maxStepHeight ||
                terrain.SampleNormal(x, z).y < slopeCos) return false;
        }
    }
    outCell.baseWalkable = outCell.walkable = true;
    return true;
}

bool FNavGrid::SampleBaseCell(const Assets::CPU::FCPUTLASSnapshot& snapshot, int gx, int gz, FNavCell& outCell) const
{
    outCell = {};
    const auto raycast = [this, &snapshot](const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 2000.0f)
    {
        return Assets::CPU::FCPUAccelerationStructure::RayCastInSnapshot(snapshot, origin, direction,
                                                                         !settings_.sampleFloorHeight, maxDistance);
    };

    const glm::vec3 worldCenter = GridToWorld(gx, gz);
    const glm::vec3 rayDir(0.0f, -1.0f, 0.0f);
    const glm::vec3 upDir(0.0f, 1.0f, 0.0f);
    const float maxSlopeCos = std::cos(glm::radians(settings_.maxSlopeAngle));

    const auto sampleCeiling = [this](float x, float z)
    {
        return settings_.sampleFloorHeight
            ? settings_.sampleFloorHeight(x, z) + settings_.floorSearchHeight
            : settings_.sampleCeiling;
    };
    const glm::vec3 rayOrigin(worldCenter.x, sampleCeiling(worldCenter.x, worldCenter.z), worldCenter.z);
    const Assets::RayCastResult centerHit = raycast(rayOrigin, rayDir);
    if (!centerHit.Hit)
    {
        return false;
    }

    const float groundY = rayOrigin.y - centerHit.T;
    outCell.groundHeight = groundY;
    if (settings_.isUnwalkable && settings_.isUnwalkable({worldCenter.x, groundY, worldCenter.z}))
    {
        return false;
    }

    const glm::vec3 normal(centerHit.Normal.x, centerHit.Normal.y, centerHit.Normal.z);
    if (glm::dot(normal, glm::vec3(0.0f, 1.0f, 0.0f)) < maxSlopeCos)
    {
        return false;
    }

    const glm::vec3 clearOrigin(worldCenter.x, groundY + 0.1f, worldCenter.z);
    const Assets::RayCastResult clearHit = raycast(clearOrigin, upDir);
    // A solid building's bottom can coincide with the terrain, so the down
    // ray alone may pick the terrain's front face. An upward back face still
    // reveals that the probe started inside a solid rather than in a room.
    if (clearHit.Hit && (clearHit.T < settings_.clearanceHeight ||
                        (settings_.sampleFloorHeight && clearHit.Normal.y > 0.0f)))
    {
        return false;
    }

    if (settings_.agentRadius > 0.001f)
    {
        const std::array<glm::vec2, 8> footprintOffsets = {
            glm::vec2(1.0f, 0.0f),
            glm::vec2(-1.0f, 0.0f),
            glm::vec2(0.0f, 1.0f),
            glm::vec2(0.0f, -1.0f),
            glm::vec2(kDiagonalInvSqrt, kDiagonalInvSqrt),
            glm::vec2(-kDiagonalInvSqrt, kDiagonalInvSqrt),
            glm::vec2(kDiagonalInvSqrt, -kDiagonalInvSqrt),
            glm::vec2(-kDiagonalInvSqrt, -kDiagonalInvSqrt)
        };

        for (const glm::vec2& unitOffset : footprintOffsets)
        {
            const glm::vec2 offset = unitOffset * settings_.agentRadius;
            const float sampleX = worldCenter.x + offset.x;
            const float sampleZ = worldCenter.z + offset.y;
            const glm::vec3 sampleOrigin(sampleX, sampleCeiling(sampleX, sampleZ), sampleZ);
            const Assets::RayCastResult sampleHit = raycast(sampleOrigin, rayDir);
            if (!sampleHit.Hit)
            {
                return false;
            }

            const float sampleGroundY = sampleOrigin.y - sampleHit.T;
            if (std::abs(sampleGroundY - groundY) > settings_.maxStepHeight)
            {
                return false;
            }

            const glm::vec3 sampleNormal(sampleHit.Normal.x, sampleHit.Normal.y, sampleHit.Normal.z);
            if (glm::dot(sampleNormal, glm::vec3(0.0f, 1.0f, 0.0f)) < maxSlopeCos)
            {
                return false;
            }

            const glm::vec3 sampleClearOrigin(sampleOrigin.x, sampleGroundY + 0.1f, sampleOrigin.z);
            // The centre's full upward ray already rejects a probe inside a
            // tall solid. Perimeter probes only need body-height clearance;
            // the horizontal footprint rays catch walls between the samples.
            const Assets::RayCastResult sampleClearHit = raycast(sampleClearOrigin, upDir, settings_.clearanceHeight);
            if (sampleClearHit.Hit && (sampleClearHit.T < settings_.clearanceHeight ||
                                      (settings_.sampleFloorHeight && sampleClearHit.Normal.y > 0.0f)))
            {
                return false;
            }

            // Down/up rays alone miss thin walls and window sills between the
            // samples. Check the body's width above its permitted step height.
            if (settings_.sampleFloorHeight)
            {
                const glm::vec3 direction(unitOffset.x, 0.0f, unitOffset.y);
                for (float height : {settings_.maxStepHeight + 0.05f, settings_.clearanceHeight * 0.5f,
                                     settings_.clearanceHeight - 0.1f})
                {
                    const auto hit = raycast(glm::vec3(worldCenter.x, groundY + height, worldCenter.z), direction,
                                             settings_.agentRadius);
                    if (hit.Hit && hit.T < settings_.agentRadius)
                    {
                        return false;
                    }
                }
            }
        }
    }

    outCell.baseWalkable = true;
    outCell.walkable = true;
    return true;
}

int FNavGrid::GetFootprintMarginCells() const
{
    if (settings_.agentRadius <= 0.001f || settings_.cellSize <= 0.001f)
    {
        return 0;
    }
    return static_cast<int>(std::ceil(settings_.agentRadius / settings_.cellSize));
}

int FNavGrid::GetExtraErosionCells() const
{
    return std::max(0, GetFootprintMarginCells() - 1);
}
}
