#pragma once

// ============================================================================
// NetHost.hpp - A UDP endpoint (ENet underneath) that is either a server
// listening for many peers or a client with one connection. Two channels:
// reliable-ordered for events and requests, unreliable-sequenced for state
// that the next packet supersedes. Single-threaded: call Poll() once per frame
// from the thread that owns the host; nothing runs in the background.
// See docs/projects/nextdayz/nextdayz-multiplayer-design.md.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace NextNet
{
    using FPeerId = uint32_t;
    inline constexpr FPeerId kInvalidPeer = 0;

    enum class EDelivery : uint8_t
    {
        Reliable,     // ordered, retransmitted (channel 0)
        Unreliable,   // sequenced, newest wins (channel 1)
    };

    struct FNetEvent
    {
        enum class EType : uint8_t
        {
            Connected,
            Disconnected,
            Message,
        };
        EType type = EType::Message;
        FPeerId peer = kInvalidPeer;
        std::vector<std::byte> payload;
    };

    struct FPeerStats
    {
        uint32_t roundTripMs = 0;
        float packetLoss = 0.0f;   // 0..1
    };

    // Everything this host sent and received (payload bytes, before ENet headers).
    struct FTrafficStats
    {
        uint64_t bytesSent = 0;
        uint64_t bytesReceived = 0;
        uint64_t messagesSent = 0;
        uint64_t messagesReceived = 0;
        uint64_t simulatedDrops = 0;
    };

    // A bad network, made on purpose: what this host sends is held back by
    // latency (+/- jitter) and unreliable messages are dropped at `lossPercent`.
    // Reliable messages are only delayed (in order), never dropped.
    struct FLinkSimulation
    {
        uint32_t latencyMs = 0;
        uint32_t jitterMs = 0;
        float lossPercent = 0.0f;
        bool Active() const { return latencyMs > 0 || jitterMs > 0 || lossPercent > 0.0f; }
    };

    class FNetHost
    {
    public:
        FNetHost();
        ~FNetHost();
        FNetHost(const FNetHost&) = delete;
        FNetHost& operator=(const FNetHost&) = delete;

        // Server: accepts up to maxPeers connections on `port` (all interfaces).
        bool Listen(uint16_t port, size_t maxPeers, std::string& error);
        // Client: starts connecting; a Connected event arrives from Poll().
        bool Connect(const std::string& host, uint16_t port, std::string& error);

        // Delivers everything that arrived since the last call, and sends queued packets.
        void Poll(std::vector<FNetEvent>& outEvents);
        bool Send(FPeerId peer, std::span<const std::byte> payload, EDelivery delivery);
        void Broadcast(std::span<const std::byte> payload, EDelivery delivery);
        void Disconnect(FPeerId peer);
        // Gracefully disconnects every peer (best effort) and releases the socket.
        void Shutdown();

        void SetLinkSimulation(const FLinkSimulation& simulation);
        const FLinkSimulation& LinkSimulation() const;
        FTrafficStats Traffic() const;

        bool IsOpen() const;
        bool IsServer() const;
        size_t PeerCount() const;
        FPeerStats Stats(FPeerId peer) const;
        // Client: the server's peer id once connected, else kInvalidPeer.
        FPeerId ServerPeer() const;

    private:
        struct FImpl;
        std::unique_ptr<FImpl> impl_;
    };
}
