#pragma once

#include "Modules/NextNet/NetHost.hpp"

#include <functional>
#include <optional>

namespace NextNet
{
    // WebRTC envelope. Game payload bytes are opaque to the transport.
    enum class ERtcFrame : uint8_t
    {
        Final = 0,
        More = 1,
        Ping = 2,
        Pong = 3,
        Close = 4,
        CloseAck = 5,
    };

    enum class ERtcPayload
    {
        Pending,
        Ready,
        Invalid,
    };

    class FRtcFraming
    {
    public:
        using FSendFrame = std::function<bool(std::span<const std::byte>)>;
        static constexpr size_t maxFrameBytes = 16 * 1024;
        static constexpr size_t maxMessageBytes = 4 * 1024 * 1024;

        bool Write(std::span<const std::byte> message, EDelivery delivery, size_t limit, const FSendFrame& send);
        ERtcPayload Read(std::vector<std::byte>& frame, EDelivery delivery, std::vector<std::byte>& message);

    private:
        uint16_t outgoingSequence_ = 0;
        std::optional<uint16_t> incomingSequence_;
        std::vector<std::byte> reliable_;
    };
}
