#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextNet/NetHost.hpp"
#include "Modules/NextNet/NetTransport.hpp"
#include "Modules/NextNet/NetWebRTC.hpp"

#include <chrono>
#include <deque>
#include <random>
#include <unordered_map>

namespace NextNet
{
    struct FNetHost::FImpl
    {
        using Clock = std::chrono::steady_clock;

        struct FSlot
        {
            std::unique_ptr<INetTransport> transport;
            std::unordered_map<FPeerId, FPeerId> ids;
        };

        struct FRoute
        {
            INetTransport* transport;
            FPeerId localPeer;
        };

        struct FDelayed
        {
            Clock::time_point release;
            bool broadcast;
            FPeerId peer;
            EDelivery delivery;
            std::vector<std::byte> payload;
        };

        std::vector<FSlot> transports;
        std::unordered_map<FPeerId, FRoute> peers;
        std::vector<FNetEvent> transportEvents;
        FLinkSimulation simulation;
        std::deque<FDelayed> delayed;
        std::unordered_set<FPeerId> disconnecting;
        Clock::time_point lastReliableRelease{};
        std::mt19937 random{0x6e657421u};
        FTrafficStats traffic;
        bool server = false;
        size_t capacity = 0;
        FPeerId nextPeerId = 1;
        FPeerId serverPeer = kInvalidPeer;
        bool connecting = false;

        bool SendNow(FPeerId id, std::span<const std::byte> payload, EDelivery delivery)
        {
            const auto it = peers.find(id);
            return it != peers.end() && it->second.transport->Send(it->second.localPeer, payload, delivery);
        }

        void BroadcastNow(std::span<const std::byte> payload, EDelivery delivery)
        {
            for (const auto& [id, peer] : peers)
            {
                if (!disconnecting.contains(id))
                {
                    peer.transport->Send(peer.localPeer, payload, delivery);
                }
            }
        }

        bool Simulate(bool broadcast, FPeerId peer, std::span<const std::byte> payload, EDelivery delivery)
        {
            if (!simulation.Active()) return false;
            if (delivery == EDelivery::Unreliable && simulation.lossPercent > 0.0f &&
                std::uniform_real_distribution<float>(0.0f, 100.0f)(random) < simulation.lossPercent)
            {
                ++traffic.simulatedDrops;
                return true;
            }
            const int jitter = simulation.jitterMs > 0
                ? std::uniform_int_distribution<int>(-static_cast<int>(simulation.jitterMs), static_cast<int>(simulation.jitterMs))(random)
                : 0;
            auto release = Clock::now() +
                std::chrono::milliseconds(std::max(0, static_cast<int>(simulation.latencyMs) + jitter));
            if (delivery == EDelivery::Reliable)
            {
                release = std::max(release, lastReliableRelease);
                lastReliableRelease = release;
            }
            delayed.push_back({release, broadcast, peer, delivery, {payload.begin(), payload.end()}});
            return true;
        }

        void ReleaseDue()
        {
            const auto now = Clock::now();
            for (auto it = delayed.begin(); it != delayed.end();)
            {
                if (it->release > now) { ++it; continue; }
                if (it->broadcast) BroadcastNow(it->payload, it->delivery);
                else SendNow(it->peer, it->payload, it->delivery);
                it = delayed.erase(it);
            }

            for (auto it = disconnecting.begin(); it != disconnecting.end();)
            {
                const auto id = *it;
                if (std::ranges::any_of(delayed, [id](const FDelayed& message)
                    { return !message.broadcast && message.peer == id && message.delivery == EDelivery::Reliable; }))
                {
                    ++it;
                    continue;
                }
                if (const auto route = peers.find(id); route != peers.end())
                {
                    route->second.transport->Disconnect(route->second.localPeer);
                }
                it = disconnecting.erase(it);
            }
        }

        void Route(FSlot& slot, FNetEvent& event, std::vector<FNetEvent>& output)
        {
            const auto it = slot.ids.find(event.peer);
            if (event.type == FNetEvent::EType::Connected)
            {
                if (it != slot.ids.end()) return;
                if (peers.size() >= capacity)
                {
                    slot.transport->Disconnect(event.peer);
                    return;
                }
                const FPeerId id = nextPeerId++;
                slot.ids.emplace(event.peer, id);
                peers.emplace(id, FRoute{slot.transport.get(), event.peer});
                event.peer = id;
                if (!server)
                {
                    serverPeer = id;
                    connecting = false;
                }
            }
            else if (it == slot.ids.end())
            {
                // An outgoing attempt can fail before the transport connects.
                if (event.type == FNetEvent::EType::Disconnected && connecting)
                {
                    connecting = false;
                    event.peer = kInvalidPeer;
                    output.push_back(std::move(event));
                }
                return;
            }
            else
            {
                event.peer = it->second;
                if (event.type == FNetEvent::EType::Disconnected)
                {
                    slot.ids.erase(it);
                    peers.erase(event.peer);
                    disconnecting.erase(event.peer);
                    if (event.peer == serverPeer) serverPeer = kInvalidPeer;
                }
                else
                {
                    traffic.bytesReceived += event.payload.size();
                    ++traffic.messagesReceived;
                }
            }
            output.push_back(std::move(event));
        }
    };

    FNetHost::FNetHost() : impl_(std::make_unique<FImpl>()) {}
    FNetHost::~FNetHost() = default;

    bool FNetHost::Listen(uint16_t port, size_t maxPeers, std::string& error)
    {
        Shutdown();
        auto transport = ListenENet(port, maxPeers, error);
        if (!transport) return false;
        FRtcServerOptions rtcOptions;
        if (!ReadWebRtcServerOptions(rtcOptions, error)) return false;
        std::unique_ptr<INetTransport> rtc;
        if (rtcOptions.port != 0)
        {
            if (rtcOptions.port == port)
            {
                error = "WebRTC and ENet need different UDP ports";
                return false;
            }
            rtc = ListenWebRtc(rtcOptions, maxPeers, error);
            if (!rtc) return false;
        }
        impl_->transports.push_back({std::move(transport), {}});
        if (rtc) impl_->transports.push_back({std::move(rtc), {}});
        impl_->server = true;
        impl_->capacity = maxPeers;
        return true;
    }

    bool FNetHost::Connect(const std::string& host, uint16_t port, std::string& error)
    {
        Shutdown();
        auto transport = ConnectENet(host, port, error);
        if (!transport) return false;
        impl_->transports.push_back({std::move(transport), {}});
        impl_->capacity = 1;
        impl_->connecting = true;
        return true;
    }

    bool FNetHost::ConnectWebRTC(const std::string& signalUrl, std::string& error)
    {
        Shutdown();
        auto transport = ConnectWebRtc(signalUrl, error);
        if (!transport) return false;
        impl_->transports.push_back({std::move(transport), {}});
        impl_->capacity = 1;
        impl_->connecting = true;
        return true;
    }

    bool FNetHost::WebRTCAvailable() { return WebRtcAvailable(); }

    void FNetHost::Poll(std::vector<FNetEvent>& outEvents)
    {
        impl_->ReleaseDue();
        auto& events = impl_->transportEvents;
        for (auto& slot : impl_->transports)
        {
            slot.transport->Poll(events);
            for (auto& event : events) impl_->Route(slot, event, outEvents);
            events.clear();
        }
    }

    bool FNetHost::Send(FPeerId peer, std::span<const std::byte> payload, EDelivery delivery)
    {
        const auto it = impl_->peers.find(peer);
        if (it == impl_->peers.end() || impl_->disconnecting.contains(peer) ||
            !it->second.transport->IsConnected(it->second.localPeer))
        {
            return false;
        }
        impl_->traffic.bytesSent += payload.size();
        ++impl_->traffic.messagesSent;
        return impl_->Simulate(false, peer, payload, delivery) || impl_->SendNow(peer, payload, delivery);
    }

    void FNetHost::Broadcast(std::span<const std::byte> payload, EDelivery delivery)
    {
        if (!IsOpen()) return;
        impl_->traffic.bytesSent += payload.size() * impl_->peers.size();
        impl_->traffic.messagesSent += impl_->peers.size();
        if (!impl_->Simulate(true, kInvalidPeer, payload, delivery))
        {
            impl_->BroadcastNow(payload, delivery);
        }
    }

    void FNetHost::Disconnect(FPeerId peer)
    {
        if (!impl_->peers.contains(peer)) return;
        // Flush a delayed reliable farewell before asking the transport to close.
        impl_->disconnecting.insert(peer);
    }

    void FNetHost::Shutdown()
    {
        impl_->transports.clear();
        impl_->peers.clear();
        impl_->delayed.clear();
        impl_->disconnecting.clear();
        impl_->lastReliableRelease = {};
        impl_->serverPeer = kInvalidPeer;
        impl_->server = false;
        impl_->connecting = false;
    }

    void FNetHost::SetLinkSimulation(const FLinkSimulation& simulation) { impl_->simulation = simulation; }
    const FLinkSimulation& FNetHost::LinkSimulation() const { return impl_->simulation; }
    FTrafficStats FNetHost::Traffic() const { return impl_->traffic; }
    bool FNetHost::IsOpen() const { return !impl_->transports.empty(); }
    bool FNetHost::IsServer() const { return impl_->server; }
    size_t FNetHost::PeerCount() const { return impl_->peers.size() + (impl_->connecting ? 1 : 0); }
    FPeerId FNetHost::ServerPeer() const { return impl_->serverPeer; }

    FPeerStats FNetHost::Stats(FPeerId peer) const
    {
        const auto it = impl_->peers.find(peer);
        return it == impl_->peers.end() ? FPeerStats{} : it->second.transport->Stats(it->second.localPeer);
    }
}
