#pragma once

#include "Modules/NextNet/NetHost.hpp"

namespace NextNet
{
    // Each transport owns its local peer IDs. Only FNetHost assigns game-facing IDs.
    class INetTransport
    {
    public:
        virtual ~INetTransport() = default;
        virtual void Poll(std::vector<FNetEvent>& events) = 0;
        virtual bool IsConnected(FPeerId peer) const = 0;
        virtual bool Send(FPeerId peer, std::span<const std::byte> payload, EDelivery delivery) = 0;
        virtual void Disconnect(FPeerId peer) = 0;
        virtual FPeerStats Stats(FPeerId peer) const = 0;
    };

    std::unique_ptr<INetTransport> ListenENet(uint16_t port, size_t maxPeers, std::string& error);
    std::unique_ptr<INetTransport> ConnectENet(const std::string& host, uint16_t port, std::string& error);
}
