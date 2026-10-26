#pragma once

// ============================================================================
// RaidenRandom.hpp - Deterministic xorshift32. Same seed, same sequence, on
// every platform and every build.
//
// std::mt19937 would do too, but the distributions around it are not specified
// bit-for-bit, and this game wants a replay to reproduce a bug report. Twelve
// lines of known arithmetic is the cheaper guarantee.
// ============================================================================

#include <cstdint>

namespace NextRaiden
{
    class FRaidenRandom
    {
    public:
        explicit FRaidenRandom(uint32_t seed = 0x5eed1234u) : state_(seed == 0u ? 0x5eed1234u : seed) {}

        void Reseed(uint32_t seed) { state_ = seed == 0u ? 0x5eed1234u : seed; }

        uint32_t Next()
        {
            state_ ^= state_ << 13;
            state_ ^= state_ >> 17;
            state_ ^= state_ << 5;
            return state_;
        }

        /// [0, 1)
        float Unit() { return static_cast<float>(Next() >> 8) / 16777216.0f; }
        /// [lo, hi)
        float Range(float lo, float hi) { return lo + (hi - lo) * Unit(); }
        /// [-1, 1)
        float Signed() { return Range(-1.0f, 1.0f); }
        /// [0, count)
        int Index(int count) { return count <= 0 ? 0 : static_cast<int>(Next() % static_cast<uint32_t>(count)); }

    private:
        uint32_t state_;
    };
}
