#pragma once

// ============================================================================
// DebugEventLog.hpp - What happened, in order, across every NextDayz system:
// infected waking up and dying, spawns and recycles, loot taken and restocked,
// noises, damage, weather turns, the messages the player was shown. A fixed
// ring buffer that always records (a few strings per second), so opening the
// debugger after something odd still shows how it came about. Identical
// consecutive events fold into one line with a repeat count. Pure data.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

namespace NextDayz::Debug
{
    enum class EChannel : uint8_t
    {
        System,
        Hud,        // what the player was told
        Player,
        Survival,
        Weapon,
        Combat,
        Infected,
        Spawn,
        Loot,
        Noise,
        World,
        Count,
    };
    inline constexpr size_t kChannelCount = static_cast<size_t>(EChannel::Count);

    const char* ChannelName(EChannel channel);

    struct FEvent
    {
        uint64_t serial = 0;         // 1-based, unique for the session
        uint64_t frame = 0;
        double time = 0.0;           // debugger clock (simulation seconds)
        EChannel channel = EChannel::System;
        int severity = 0;            // 0 info, 1 notable, 2 warning, 3 error
        std::string text;
        bool hasPosition = false;
        glm::vec3 position{0.0f};
        uint32_t repeats = 1;
    };

    class EventLog
    {
    public:
        explicit EventLog(size_t capacity = 2048);

        void SetClock(uint64_t frame, double time)
        {
            frame_ = frame;
            time_ = time;
        }
        // Appends (or folds into the newest line when channel and text match).
        const FEvent& Push(EChannel channel, int severity, std::string text, const glm::vec3* position = nullptr);
        void Clear();

        size_t Size() const { return size_; }
        size_t Capacity() const { return ring_.size(); }
        // 0 = oldest kept event.
        const FEvent& At(size_t index) const { return ring_[(head_ + index) % ring_.size()]; }
        const FEvent* Newest() const { return size_ > 0 ? &At(size_ - 1) : nullptr; }
        uint64_t TotalPushed() const { return serial_; }
        uint32_t ChannelCount(EChannel channel) const { return channelCounts_[static_cast<size_t>(channel)]; }

    private:
        std::vector<FEvent> ring_;
        size_t head_ = 0;
        size_t size_ = 0;
        uint64_t serial_ = 0;
        uint64_t frame_ = 0;
        double time_ = 0.0;
        std::array<uint32_t, kChannelCount> channelCounts_{};
    };
}
