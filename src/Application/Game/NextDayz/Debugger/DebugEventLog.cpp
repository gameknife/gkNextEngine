#include "Engine/Common/CoreMinimal.hpp"

#include "DebugEventLog.hpp"

namespace NextDayz::Debug
{
    const char* ChannelName(EChannel channel)
    {
        switch (channel)
        {
        case EChannel::System: return "System";
        case EChannel::Hud: return "HUD";
        case EChannel::Player: return "Player";
        case EChannel::Survival: return "Survival";
        case EChannel::Weapon: return "Weapon";
        case EChannel::Combat: return "Combat";
        case EChannel::Infected: return "Infected";
        case EChannel::Spawn: return "Spawn";
        case EChannel::Loot: return "Loot";
        case EChannel::Noise: return "Noise";
        case EChannel::World: return "World";
        case EChannel::Count: break;
        }
        return "?";
    }

    EventLog::EventLog(size_t capacity)
        : ring_(std::max<size_t>(capacity, 1))
    {
    }

    const FEvent& EventLog::Push(EChannel channel, int severity, std::string text, const glm::vec3* position)
    {
        ++channelCounts_[static_cast<size_t>(channel)];
        if (size_ > 0)
        {
            FEvent& newest = ring_[(head_ + size_ - 1) % ring_.size()];
            if (newest.channel == channel && newest.text == text)
            {
                ++newest.repeats;
                newest.frame = frame_;
                newest.time = time_;
                newest.severity = std::max(newest.severity, severity);
                if (position)
                {
                    newest.hasPosition = true;
                    newest.position = *position;
                }
                return newest;
            }
        }

        size_t slot = 0;
        if (size_ < ring_.size())
        {
            slot = (head_ + size_) % ring_.size();
            ++size_;
        }
        else
        {
            slot = head_;
            head_ = (head_ + 1) % ring_.size();
        }
        FEvent& event = ring_[slot];
        event.serial = ++serial_;
        event.frame = frame_;
        event.time = time_;
        event.channel = channel;
        event.severity = severity;
        event.text = std::move(text);
        event.hasPosition = position != nullptr;
        event.position = position ? *position : glm::vec3(0.0f);
        event.repeats = 1;
        return event;
    }

    void EventLog::Clear()
    {
        head_ = 0;
        size_ = 0;
        channelCounts_ = {};
    }
}
