#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextNet/NetHost.hpp"

#include <enet/enet.h>

#include <mutex>
#include <unordered_map>

namespace NextNet
{
    namespace
    {
        constexpr size_t kChannelCount = 2;

        // ENet keeps process-wide socket state (WSAStartup on Windows).
        std::mutex libraryMutex;
        int libraryUsers = 0;

        bool AcquireLibrary()
        {
            std::lock_guard lock(libraryMutex);
            if (libraryUsers == 0 && enet_initialize() != 0)
            {
                return false;
            }
            ++libraryUsers;
            return true;
        }

        void ReleaseLibrary()
        {
            std::lock_guard lock(libraryMutex);
            if (--libraryUsers == 0)
            {
                enet_deinitialize();
            }
        }

        uint8_t ChannelOf(EDelivery delivery) { return delivery == EDelivery::Reliable ? 0 : 1; }

        ENetPacket* MakePacket(std::span<const std::byte> payload, EDelivery delivery)
        {
            return enet_packet_create(payload.data(), payload.size(),
                                      delivery == EDelivery::Reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
        }
    }

    struct FNetHost::FImpl
    {
        ENetHost* host = nullptr;
        bool server = false;
        bool libraryHeld = false;
        FPeerId nextPeerId = 1;
        FPeerId serverPeer = kInvalidPeer;
        std::unordered_map<FPeerId, ENetPeer*> peers;

        FPeerId IdOf(const ENetPeer* peer) const
        {
            return static_cast<FPeerId>(reinterpret_cast<uintptr_t>(peer->data));
        }

        ENetPeer* Find(FPeerId id) const
        {
            const auto it = peers.find(id);
            return it == peers.end() ? nullptr : it->second;
        }

        FPeerId Adopt(ENetPeer* peer)
        {
            const FPeerId id = nextPeerId++;
            peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
            peers[id] = peer;
            return id;
        }
    };

    FNetHost::FNetHost() : impl_(std::make_unique<FImpl>()) {}

    FNetHost::~FNetHost()
    {
        Shutdown();
    }

    bool FNetHost::Listen(uint16_t port, size_t maxPeers, std::string& error)
    {
        Shutdown();
        if (!AcquireLibrary())
        {
            error = "enet_initialize failed";
            return false;
        }
        impl_->libraryHeld = true;
        ENetAddress address{};
        address.host = ENET_HOST_ANY;
        address.port = port;
        impl_->host = enet_host_create(&address, maxPeers, kChannelCount, 0, 0);
        if (!impl_->host)
        {
            error = fmt::format("cannot listen on UDP port {} (in use?)", port);
            Shutdown();
            return false;
        }
        impl_->server = true;
        return true;
    }

    bool FNetHost::Connect(const std::string& host, uint16_t port, std::string& error)
    {
        Shutdown();
        if (!AcquireLibrary())
        {
            error = "enet_initialize failed";
            return false;
        }
        impl_->libraryHeld = true;
        impl_->host = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
        if (!impl_->host)
        {
            error = "cannot create a client socket";
            Shutdown();
            return false;
        }
        ENetAddress address{};
        if (enet_address_set_host(&address, host.c_str()) != 0)
        {
            error = fmt::format("cannot resolve '{}'", host);
            Shutdown();
            return false;
        }
        address.port = port;
        ENetPeer* peer = enet_host_connect(impl_->host, &address, kChannelCount, 0);
        if (!peer)
        {
            error = "no free peer slot for the connection";
            Shutdown();
            return false;
        }
        impl_->Adopt(peer);
        impl_->server = false;
        return true;
    }

    void FNetHost::Poll(std::vector<FNetEvent>& outEvents)
    {
        if (!impl_->host)
        {
            return;
        }
        ENetEvent event{};
        while (enet_host_service(impl_->host, &event, 0) > 0)
        {
            switch (event.type)
            {
            case ENET_EVENT_TYPE_CONNECT:
            {
                // A client already adopted its outgoing peer; a server adopts newcomers here.
                FPeerId id = event.peer->data ? impl_->IdOf(event.peer) : impl_->Adopt(event.peer);
                if (!impl_->server) impl_->serverPeer = id;
                outEvents.push_back({FNetEvent::EType::Connected, id, {}});
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT:
            {
                const FPeerId id = event.peer->data ? impl_->IdOf(event.peer) : kInvalidPeer;
                impl_->peers.erase(id);
                event.peer->data = nullptr;
                if (id == impl_->serverPeer) impl_->serverPeer = kInvalidPeer;
                outEvents.push_back({FNetEvent::EType::Disconnected, id, {}});
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE:
            {
                FNetEvent message{FNetEvent::EType::Message, impl_->IdOf(event.peer), {}};
                const auto* bytes = reinterpret_cast<const std::byte*>(event.packet->data);
                message.payload.assign(bytes, bytes + event.packet->dataLength);
                enet_packet_destroy(event.packet);
                outEvents.push_back(std::move(message));
                break;
            }
            case ENET_EVENT_TYPE_NONE:
                break;
            }
        }
        enet_host_flush(impl_->host);
    }

    bool FNetHost::Send(FPeerId peer, std::span<const std::byte> payload, EDelivery delivery)
    {
        ENetPeer* target = impl_->Find(peer);
        if (!target || target->state != ENET_PEER_STATE_CONNECTED)
        {
            return false;
        }
        ENetPacket* packet = MakePacket(payload, delivery);
        if (enet_peer_send(target, ChannelOf(delivery), packet) != 0)
        {
            enet_packet_destroy(packet);
            return false;
        }
        return true;
    }

    void FNetHost::Broadcast(std::span<const std::byte> payload, EDelivery delivery)
    {
        if (!impl_->host)
        {
            return;
        }
        enet_host_broadcast(impl_->host, ChannelOf(delivery), MakePacket(payload, delivery));
    }

    void FNetHost::Disconnect(FPeerId peer)
    {
        if (ENetPeer* target = impl_->Find(peer))
        {
            // After what is already queued for them (a farewell, a refusal), not instead of it.
            enet_peer_disconnect_later(target, 0);
        }
    }

    void FNetHost::Shutdown()
    {
        if (impl_->host)
        {
            for (auto& [id, peer] : impl_->peers)
            {
                enet_peer_disconnect_now(peer, 0);
            }
            enet_host_flush(impl_->host);
            enet_host_destroy(impl_->host);
            impl_->host = nullptr;
        }
        impl_->peers.clear();
        impl_->serverPeer = kInvalidPeer;
        if (impl_->libraryHeld)
        {
            ReleaseLibrary();
            impl_->libraryHeld = false;
        }
    }

    bool FNetHost::IsOpen() const { return impl_->host != nullptr; }
    bool FNetHost::IsServer() const { return impl_->server; }
    size_t FNetHost::PeerCount() const { return impl_->peers.size(); }
    FPeerId FNetHost::ServerPeer() const { return impl_->serverPeer; }

    FPeerStats FNetHost::Stats(FPeerId peer) const
    {
        FPeerStats stats;
        if (const ENetPeer* target = impl_->Find(peer))
        {
            stats.roundTripMs = target->roundTripTime;
            stats.packetLoss = static_cast<float>(target->packetLoss) / static_cast<float>(ENET_PEER_PACKET_LOSS_SCALE);
        }
        return stats;
    }
}
