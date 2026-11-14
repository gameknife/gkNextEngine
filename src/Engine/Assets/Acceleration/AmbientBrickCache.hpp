#pragma once

// In-memory cache of baked ambient-cube bricks, keyed by cascade and world brick coordinate.
//
// Every cascade's bricks sit on that cascade's world lattice, so a brick's lighting depends only on
// the scene, the cascade and where the brick is in the world -- not on where the window happened to
// be when it was baked. When a window scrolls away from a brick that was already published, its
// probes are read back here; when the window comes back, the brick is uploaded again with the passes
// it had, instead of starting from zero. Walking back and forth or teleporting to a place visited
// before therefore costs no bake.
//
// Entries are evicted least recently used first once the byte budget is exceeded, around any
// geometry change (EvictBox), and all at once whenever the grid or the lighting itself is reset.

#include "Engine/Assets/GPU/UniformBuffer.hpp"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <list>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#if defined(_M_X64) || (defined(__x86_64__) && defined(__SSE4_1__))
#include <smmintrin.h>
#define GK_AMBIENT_STREAMING_READ 1
#endif

namespace Assets::CPU
{
    struct FAmbientBrickCacheStats
    {
        uint32_t entries = 0;
        uint64_t bytes = 0;
        uint64_t hits = 0;
        uint64_t stores = 0;
        uint64_t evictions = 0;
    };

    // Copies out of host-visible device memory. That memory is write-combined: ordinary loads are
    // uncached and each one is a bus round trip, so a plain memcpy of a few thousand bricks takes
    // seconds. Streaming loads (MOVNTDQA) fetch whole lines instead. Both pointers 16-byte aligned.
    inline void CopyFromDeviceMemory(void* dst, const void* src, size_t bytes)
    {
#if GK_AMBIENT_STREAMING_READ
        auto* out = static_cast<__m128i*>(dst);
        auto* in = static_cast<__m128i*>(const_cast<void*>(src));
        const size_t vectors = bytes / sizeof(__m128i);
        size_t i = 0;
        for (; i + 4 <= vectors; i += 4)
        {
            const __m128i a = _mm_stream_load_si128(in + i);
            const __m128i b = _mm_stream_load_si128(in + i + 1);
            const __m128i c = _mm_stream_load_si128(in + i + 2);
            const __m128i d = _mm_stream_load_si128(in + i + 3);
            _mm_storeu_si128(out + i, a);
            _mm_storeu_si128(out + i + 1, b);
            _mm_storeu_si128(out + i + 2, c);
            _mm_storeu_si128(out + i + 3, d);
        }
        for (; i < vectors; ++i)
        {
            _mm_storeu_si128(out + i, _mm_stream_load_si128(in + i));
        }
        const size_t tail = bytes - vectors * sizeof(__m128i);
        if (tail != 0)
        {
            std::memcpy(static_cast<std::byte*>(dst) + vectors * sizeof(__m128i),
                        static_cast<const std::byte*>(src) + vectors * sizeof(__m128i), tail);
        }
#else
        std::memcpy(dst, src, bytes);
#endif
    }

    class FAmbientBrickCache
    {
    public:
        static constexpr uint32_t kProbesPerBrick = static_cast<uint32_t>(GPU_SCENE_AMBIENT_BRICK_VOLUME);
        static constexpr uint64_t kBytesPerEntry = sizeof(AmbientCube) * kProbesPerBrick;

        // 20 bits per axis (signed), 4 bits of cascade: a million bricks either way of the origin.
        static uint64_t MakeKey(uint32_t cascade, const glm::ivec3& worldBrick)
        {
            const auto axis = [](int value) { return static_cast<uint64_t>(static_cast<uint32_t>(value) & 0xFFFFFu); };
            return (static_cast<uint64_t>(cascade & 0xFu) << 60u) | (axis(worldBrick.x) << 40u) |
                   (axis(worldBrick.y) << 20u) | axis(worldBrick.z);
        }

        void SetBudgetBytes(uint64_t budget)
        {
            budgetBytes_ = budget;
            Trim();
        }
        uint64_t BudgetBytes() const { return budgetBytes_; }
        bool Enabled() const { return budgetBytes_ >= kBytesPerEntry; }

        // Takes one brick's probes (kProbesPerBrick cubes) with the bake passes it has had.
        void Store(uint32_t cascade, const glm::ivec3& worldBrick, std::vector<AmbientCube>&& probes, uint8_t passes)
        {
            if (!Enabled() || probes.size() != kProbesPerBrick)
            {
                return;
            }
            const uint64_t key = MakeKey(cascade, worldBrick);
            auto it = entries_.find(key);
            if (it == entries_.end())
            {
                lru_.push_front(key);
                it = entries_.emplace(key, FEntry{lru_.begin(), {}, 0u, worldBrick}).first;
            }
            else
            {
                lru_.splice(lru_.begin(), lru_, it->second.lruIt);
            }
            it->second.probes = std::move(probes);
            it->second.passes = passes;
            ++stats_.stores;
            Trim();
        }

        // Copies one brick's probes in.
        void Store(uint32_t cascade, const glm::ivec3& worldBrick, const AmbientCube* probes, uint8_t passes)
        {
            if (!Enabled())
            {
                return;
            }
            const uint64_t key = MakeKey(cascade, worldBrick);
            auto it = entries_.find(key);
            if (it == entries_.end())
            {
                lru_.push_front(key);
                it = entries_.emplace(key, FEntry{lru_.begin(), {}, 0u, worldBrick}).first;
                it->second.probes.resize(kProbesPerBrick);
            }
            else
            {
                lru_.splice(lru_.begin(), lru_, it->second.lruIt);
            }
            std::memcpy(it->second.probes.data(), probes, kBytesPerEntry);
            it->second.passes = passes;
            ++stats_.stores;
            Trim();
        }

        // Copies a cached brick out and marks it most recently used. The entry stays: the window may
        // leave again and the copy saves the readback.
        bool Fetch(uint32_t cascade, const glm::ivec3& worldBrick, AmbientCube* outProbes, uint8_t& outPasses)
        {
            const auto it = entries_.find(MakeKey(cascade, worldBrick));
            if (it == entries_.end())
            {
                return false;
            }
            lru_.splice(lru_.begin(), lru_, it->second.lruIt);
            std::memcpy(outProbes, it->second.probes.data(), kBytesPerEntry);
            outPasses = it->second.passes;
            ++stats_.hits;
            return true;
        }

        bool Contains(uint32_t cascade, const glm::ivec3& worldBrick) const
        {
            return entries_.find(MakeKey(cascade, worldBrick)) != entries_.end();
        }

        // Drops every brick of `cascade` within the inclusive world-brick box.
        void EvictBox(uint32_t cascade, const glm::ivec3& brickMin, const glm::ivec3& brickMax)
        {
            for (auto it = entries_.begin(); it != entries_.end();)
            {
                const glm::ivec3& brick = it->second.worldBrick;
                const bool inside = static_cast<uint32_t>(it->first >> 60u) == (cascade & 0xFu) &&
                                    glm::all(glm::greaterThanEqual(brick, brickMin)) &&
                                    glm::all(glm::lessThanEqual(brick, brickMax));
                if (inside)
                {
                    lru_.erase(it->second.lruIt);
                    it = entries_.erase(it);
                    ++stats_.evictions;
                }
                else
                {
                    ++it;
                }
            }
        }

        void Clear()
        {
            stats_.evictions += entries_.size();
            entries_.clear();
            lru_.clear();
        }

        FAmbientBrickCacheStats Stats() const
        {
            FAmbientBrickCacheStats stats = stats_;
            stats.entries = static_cast<uint32_t>(entries_.size());
            stats.bytes = static_cast<uint64_t>(entries_.size()) * kBytesPerEntry;
            return stats;
        }

    private:
        struct FEntry
        {
            std::list<uint64_t>::iterator lruIt;
            std::vector<AmbientCube> probes;
            uint8_t passes = 0;
            glm::ivec3 worldBrick{0};
        };

        void Trim()
        {
            while (!lru_.empty() && static_cast<uint64_t>(entries_.size()) * kBytesPerEntry > budgetBytes_)
            {
                entries_.erase(lru_.back());
                lru_.pop_back();
                ++stats_.evictions;
            }
        }

        uint64_t budgetBytes_ = 0;
        std::list<uint64_t> lru_;
        std::unordered_map<uint64_t, FEntry> entries_;
        FAmbientBrickCacheStats stats_;
    };
}
