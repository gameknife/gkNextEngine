#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    using Json = nlohmann::json;

    struct Peer
    {
        std::mutex mutex;
        std::atomic<size_t> unreliableReceived = 0;
        std::vector<std::shared_ptr<rtc::DataChannel>> channels;
        std::shared_ptr<rtc::PeerConnection> connection;
    };

    std::shared_ptr<Peer> Accept(const std::string& offer, uint16_t port, bool useStun)
    {
        rtc::Configuration config;
        config.enableIceUdpMux = true;
        config.portRangeBegin = port;
        config.portRangeEnd = port;
        config.mtu = 1200;
        config.maxMessageSize = 256 * 1024;
        if (useStun)
        {
            const char* stun = std::getenv("ND_W0_STUN");
            if (!stun)
            {
                throw std::runtime_error("Set ND_W0_STUN to test a public ICE candidate");
            }
            config.iceServers.emplace_back(stun);
        }

        auto peer = std::make_shared<Peer>();
        peer->connection = std::make_shared<rtc::PeerConnection>(config);
        peer->connection->onDataChannel([weakPeer = std::weak_ptr(peer)](auto channel)
        {
            auto owner = weakPeer.lock();
            if (!owner) return;
            channel->onMessage([weakPeer, weakChannel = std::weak_ptr(channel)](rtc::message_variant message)
            {
                if (auto active = weakChannel.lock(); active && active->isOpen())
                {
                    if (active->label() == "u")
                    {
                        if (auto owner = weakPeer.lock()) ++owner->unreliableReceived;
                    }
                    active->send(std::move(message));
                }
            });
            std::scoped_lock lock(owner->mutex);
            owner->channels.push_back(std::move(channel));
        });

        auto gathered = std::make_shared<std::promise<void>>();
        auto ready = gathered->get_future();
        peer->connection->onGatheringStateChange([gathered](rtc::PeerConnection::GatheringState state)
        {
            if (state == rtc::PeerConnection::GatheringState::Complete)
            {
                gathered->set_value();
            }
        });
        peer->connection->setRemoteDescription(rtc::Description(offer, "offer"));
        if (ready.wait_for(std::chrono::seconds(12)) != std::future_status::ready || !peer->connection->localDescription())
        {
            throw std::runtime_error("ICE answer timed out");
        }
        return peer;
    }
}

int main(int argc, const char* argv[])
{
    if (argc != 2) return 2;
    const int port = std::stoi(argv[1]);
    if (port < 1024 || port > 65535) return 2;

    std::map<int, std::shared_ptr<Peer>> peers;
    std::string line;
    while (std::getline(std::cin, line))
    {
        Json reply;
        try
        {
            const auto request = Json::parse(line);
            reply["request"] = request.at("request");
            const std::string method = request.at("method");
            if (method == "offer")
            {
                std::erase_if(peers, [](const auto& entry)
                {
                    const auto state = entry.second->connection->state();
                    return state == rtc::PeerConnection::State::Closed ||
                           state == rtc::PeerConnection::State::Failed ||
                           state == rtc::PeerConnection::State::Disconnected;
                });
                if (peers.size() >= 8) throw std::runtime_error("Probe peer limit reached");
                const auto peer = Accept(request.at("sdp"), port, request.value("stun", false));
                const int id = request.at("request");
                peers.emplace(id, peer);
                reply["id"] = id;
                reply["sdp"] = std::string(*peer->connection->localDescription());
            }
            else if (method == "stats")
            {
                auto peer = peers.at(request.at("id"));
                rtc::Candidate local;
                rtc::Candidate remote;
                if (!peer->connection->getSelectedCandidatePair(&local, &remote))
                {
                    throw std::runtime_error("ICE selected pair unavailable");
                }
                reply["local"] = std::string(local);
                reply["remote"] = std::string(remote);
                reply["port"] = local.port().value_or(0);
                reply["maxMessageSize"] = peer->connection->remoteMaxMessageSize();
                reply["unreliableReceived"] = peer->unreliableReceived.load();
            }
            else if (method == "close")
            {
                const int id = request.at("id");
                if (const auto found = peers.find(id); found != peers.end())
                {
                    found->second->connection->close();
                    peers.erase(found);
                }
            }
        }
        catch (const std::exception& error)
        {
            reply["error"] = error.what();
        }
        std::cout << reply.dump() << std::endl;
    }
}
