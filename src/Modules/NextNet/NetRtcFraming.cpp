#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextNet/NetRtcFraming.hpp"

namespace NextNet
{
    bool FRtcFraming::Write(std::span<const std::byte> message, EDelivery delivery, size_t limit, const FSendFrame& send)
    {
        limit = std::min(limit, maxFrameBytes);
        if (limit <= 2 || message.size() > maxMessageBytes) return false;
        std::vector<std::byte> frame;
        if (delivery == EDelivery::Unreliable)
        {
            if (message.size() > limit - 2) return false;
            const uint16_t sequence = outgoingSequence_++;
            frame.reserve(message.size() + 2);
            frame.push_back(static_cast<std::byte>(sequence & 0xff));
            frame.push_back(static_cast<std::byte>(sequence >> 8));
            frame.insert(frame.end(), message.begin(), message.end());
            return send(frame);
        }

        frame.reserve(std::min(message.size() + 1, limit));
        do
        {
            const size_t length = std::min(message.size(), limit - 1);
            frame.clear();
            frame.push_back(static_cast<std::byte>(length == message.size() ? ERtcFrame::Final : ERtcFrame::More));
            frame.insert(frame.end(), message.begin(), message.begin() + length);
            if (!send(frame)) return false;
            message = message.subspan(length);
        }
        while (!message.empty());
        return true;
    }

    ERtcPayload FRtcFraming::Read(std::vector<std::byte>& frame, EDelivery delivery, std::vector<std::byte>& message)
    {
        if (frame.empty() || frame.size() > maxFrameBytes) return ERtcPayload::Invalid;
        if (delivery == EDelivery::Unreliable)
        {
            if (frame.size() < 2) return ERtcPayload::Invalid;
            const auto sequence = static_cast<uint16_t>(std::to_integer<uint16_t>(frame[0]) |
                                                        (std::to_integer<uint16_t>(frame[1]) << 8));
            if (incomingSequence_)
            {
                const auto ahead = static_cast<uint16_t>(sequence - *incomingSequence_);
                if (ahead == 0 || ahead >= 0x8000) return ERtcPayload::Pending;
            }
            incomingSequence_ = sequence;
            frame.erase(frame.begin(), frame.begin() + 2);
            message = std::move(frame);
            return ERtcPayload::Ready;
        }

        const auto kind = static_cast<ERtcFrame>(frame[0]);
        if (kind != ERtcFrame::More && kind != ERtcFrame::Final) return ERtcPayload::Invalid;
        const size_t bodyBytes = frame.size() - 1;
        if (bodyBytes > maxMessageBytes - reliable_.size() || (kind == ERtcFrame::More && bodyBytes == 0))
        {
            return ERtcPayload::Invalid;
        }
        if (kind == ERtcFrame::Final && reliable_.empty())
        {
            frame.erase(frame.begin());
            message = std::move(frame);
            return ERtcPayload::Ready;
        }
        reliable_.insert(reliable_.end(), frame.begin() + 1, frame.end());
        if (kind == ERtcFrame::More) return ERtcPayload::Pending;
        message = std::exchange(reliable_, {});
        return ERtcPayload::Ready;
    }
}
