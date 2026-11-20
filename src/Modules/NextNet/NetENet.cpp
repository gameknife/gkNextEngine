#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextNet/NetTransport.hpp"

#include <enet/enet.h>

#include <mutex>

namespace NextNet
{
    namespace
    {
        constexpr size_t channelCount = 2;

        // ENet keeps process-wide socket state (WSAStartup on Windows).
        std::mutex enetLibraryMutex;
        int enetLibraryUsers = 0;

        class FENetTransport final : public INetTransport
        {
        public:
            bool Open(const ENetAddress* address, size_t maxPeers, std::string& error)
            {
                {
                    std::lock_guard lock(enetLibraryMutex);
                    if (enetLibraryUsers == 0 && enet_initialize() != 0)
                    {
                        error = "enet_initialize failed";
                        return false;
                    }
                    ++enetLibraryUsers;
                    libraryHeld_ = true;
                }
                host_ = enet_host_create(address, maxPeers, channelCount, 0, 0);
                if (!host_)
                {
                    error = address ? fmt::format("cannot listen on UDP port {} (in use?)", address->port)
                                    : "cannot create a client socket";
                }
                return host_ != nullptr;
            }

            bool Connect(const std::string& hostname, uint16_t port, std::string& error)
            {
                ENetAddress address{};
                if (enet_address_set_host(&address, hostname.c_str()) != 0)
                {
                    error = fmt::format("cannot resolve '{}'", hostname);
                    return false;
                }
                address.port = port;
                if (!enet_host_connect(host_, &address, channelCount, 0))
                {
                    error = "no free peer slot for the connection";
                    return false;
                }
                return true;
            }

            ~FENetTransport() override
            {
                if (host_)
                {
                    for (size_t i = 0; i < host_->peerCount; ++i)
                    {
                        enet_peer_disconnect_now(&host_->peers[i], 0);
                    }
                    enet_host_flush(host_);
                    enet_host_destroy(host_);
                }
                if (libraryHeld_)
                {
                    std::lock_guard lock(enetLibraryMutex);
                    if (--enetLibraryUsers == 0)
                    {
                        enet_deinitialize();
                    }
                }
            }

            void Poll(std::vector<FNetEvent>& events) override
            {
                ENetEvent event{};
                while (enet_host_service(host_, &event, 0) > 0)
                {
                    const auto id = static_cast<FPeerId>(event.peer - host_->peers) + 1;
                    switch (event.type)
                    {
                    case ENET_EVENT_TYPE_CONNECT:
                        events.push_back({FNetEvent::EType::Connected, id, {}});
                        break;
                    case ENET_EVENT_TYPE_DISCONNECT:
                        events.push_back({FNetEvent::EType::Disconnected, id, {}});
                        break;
                    case ENET_EVENT_TYPE_RECEIVE:
                    {
                        const auto* bytes = reinterpret_cast<const std::byte*>(event.packet->data);
                        events.push_back({FNetEvent::EType::Message, id, {bytes, bytes + event.packet->dataLength}});
                        enet_packet_destroy(event.packet);
                        break;
                    }
                    case ENET_EVENT_TYPE_NONE:
                        break;
                    }
                }
                enet_host_flush(host_);
            }

            bool IsConnected(FPeerId peer) const override
            {
                return host_->peers[peer - 1].state == ENET_PEER_STATE_CONNECTED;
            }

            bool Send(FPeerId peer, std::span<const std::byte> payload, EDelivery delivery) override
            {
                if (!IsConnected(peer)) return false;
                const bool reliable = delivery == EDelivery::Reliable;
                ENetPacket* packet = enet_packet_create(payload.data(), payload.size(),
                    reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
                if (!packet) return false;
                if (enet_peer_send(&host_->peers[peer - 1], reliable ? 0 : 1, packet) != 0)
                {
                    enet_packet_destroy(packet);
                    return false;
                }
                return true;
            }

            void Disconnect(FPeerId peer) override
            {
                enet_peer_disconnect_later(&host_->peers[peer - 1], 0);
            }

            FPeerStats Stats(FPeerId peer) const override
            {
                const auto& target = host_->peers[peer - 1];
                return {target.roundTripTime,
                    static_cast<float>(target.packetLoss) / static_cast<float>(ENET_PEER_PACKET_LOSS_SCALE)};
            }

        private:
            ENetHost* host_ = nullptr;
            bool libraryHeld_ = false;
        };
    }

    std::unique_ptr<INetTransport> ListenENet(uint16_t port, size_t maxPeers, std::string& error)
    {
        auto transport = std::make_unique<FENetTransport>();
        ENetAddress address{ENET_HOST_ANY, port};
        if (!transport->Open(&address, maxPeers, error)) return {};
        return transport;
    }

    std::unique_ptr<INetTransport> ConnectENet(const std::string& host, uint16_t port, std::string& error)
    {
        auto transport = std::make_unique<FENetTransport>();
        if (!transport->Open(nullptr, 1, error) || !transport->Connect(host, port, error)) return {};
        return transport;
    }
}
