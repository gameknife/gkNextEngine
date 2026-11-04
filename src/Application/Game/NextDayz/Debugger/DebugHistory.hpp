#pragma once

// ============================================================================
// DebugHistory.hpp - Fixed-length sample series for the debugger's plots and
// derived rates (hunger per minute, infected alive over time, frame cost).
// A ring buffer laid out the way ImGui::PlotLines reads one (values + offset).
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

namespace NextDayz::Debug
{
    template <size_t N>
    class TSeries
    {
    public:
        static constexpr size_t kCapacity = N;

        void Push(float value)
        {
            values_[next_] = value;
            next_ = (next_ + 1) % N;
            count_ = std::min(count_ + 1, N);
        }
        void Clear()
        {
            next_ = 0;
            count_ = 0;
        }

        size_t Count() const { return count_; }
        bool Empty() const { return count_ == 0; }
        // 0 = oldest kept sample.
        float At(size_t index) const { return values_[(Start() + index) % N]; }
        float Latest() const { return count_ > 0 ? At(count_ - 1) : 0.0f; }
        // The sample `samplesBack` before the newest (clamped to the oldest).
        float Back(size_t samplesBack) const
        {
            if (count_ == 0)
            {
                return 0.0f;
            }
            return At(count_ - 1 - std::min(samplesBack, count_ - 1));
        }
        float Min() const
        {
            float result = count_ > 0 ? At(0) : 0.0f;
            for (size_t index = 1; index < count_; ++index)
            {
                result = std::min(result, At(index));
            }
            return result;
        }
        float Max() const
        {
            float result = count_ > 0 ? At(0) : 0.0f;
            for (size_t index = 1; index < count_; ++index)
            {
                result = std::max(result, At(index));
            }
            return result;
        }

        // ImGui::PlotLines(values, PlotCount(), PlotOffset()).
        const float* PlotValues() const { return values_.data(); }
        int PlotCount() const { return static_cast<int>(count_); }
        int PlotOffset() const { return count_ < N ? 0 : static_cast<int>(next_); }

    private:
        size_t Start() const { return count_ < N ? 0 : next_; }

        std::array<float, N> values_{};
        size_t next_ = 0;
        size_t count_ = 0;
    };
}
