#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextNet/NetWebRTC.hpp"

#include <charconv>
#include <cstdlib>

#if GK_NEXTNET_RTC
#include "Modules/NextNet/NetBuffer.hpp"
#include "Modules/NextNet/NetHttp.hpp"
#include "Modules/NextNet/NetRtcFraming.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#endif

namespace NextNet
{
    namespace
    {
        std::string RtcEnv(const char* name)
        {
            const char* value = std::getenv(name);
            return value ? value : "";
        }

        bool RtcEnvPort(const char* name, uint16_t& port, std::string& error)
        {
            const auto text = RtcEnv(name);
            if (text.empty()) return true;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), port);
            if (result.ec == std::errc{} && result.ptr == text.data() + text.size() && port != 0) return true;
            error = fmt::format("{} must be a port in 1..65535", name);
            return false;
        }
    }

    bool ReadWebRtcServerOptions(FRtcServerOptions& options, std::string& error)
    {
        if (!RtcEnvPort("GK_NEXTNET_RTC_PORT", options.port, error)) return false;
        if (options.port == 0) return true;
        if (!RtcEnvPort("GK_NEXTNET_RTC_SIGNAL_PORT", options.signalPort, error)) return false;
        options.webRoot = RtcEnv("GK_NEXTNET_RTC_WEB_ROOT");
        options.masterUrl = RtcEnv("GK_NEXTNET_RTC_MASTER");
        options.serverId = RtcEnv("GK_NEXTNET_RTC_DS_ID");
        options.serverKey = RtcEnv("GK_NEXTNET_RTC_DS_KEY");
        options.publicHost = RtcEnv("GK_NEXTNET_RTC_PUBLIC_HOST");
        options.stun = RtcEnv("GK_NEXTNET_RTC_STUN");
        if (options.signalPort == 0 && (options.masterUrl.empty() || options.serverId.empty() || options.serverKey.empty()))
        {
            error = "WebRTC needs a local signaling port or Master URL and DS identity";
            return false;
        }
        return true;
    }

    bool WebRtcAvailable()
    {
#if GK_NEXTNET_RTC
        return true;
#else
        return false;
#endif
    }

#if GK_NEXTNET_RTC
    namespace
    {
        using RtcClock = std::chrono::steady_clock;
        using RtcJson = nlohmann::json;
        constexpr auto gatherTimeout = std::chrono::seconds(6);
        constexpr auto connectionTimeout = std::chrono::seconds(25);
        constexpr auto closeTimeout = std::chrono::seconds(2);
        constexpr size_t maxSignalBytes = 64 * 1024;
        constexpr size_t maxQueuedBytes = 2 * FRtcFraming::maxMessageBytes;

        struct FRtcIncoming
        {
            EDelivery delivery;
            rtc::binary bytes;
        };

        struct FRtcPeer
        {
            const FPeerId id;
            const std::shared_ptr<rtc::PeerConnection> connection;
            const RtcClock::time_point created = RtcClock::now();

            // libdatachannel's callbacks are producers only. Poll() binds
            // delivered channels and consumes frames on the host thread.
            std::mutex mutex;
            std::condition_variable gathering;
            bool gathered = false;
            bool failed = false;
            size_t queuedBytes = 0;
            std::vector<std::shared_ptr<rtc::DataChannel>> arrivingChannels;
            std::vector<FRtcIncoming> arrivingFrames;

            // Host-thread state.
            std::shared_ptr<rtc::DataChannel> reliable;
            std::shared_ptr<rtc::DataChannel> unreliable;
            FRtcFraming framing;
            RtcClock::time_point pingSent{};
            RtcClock::time_point closeAt{};
            uint32_t pingId = 0;
            uint32_t roundTripMs = 0;
            bool connected = false;

            FRtcPeer(FPeerId peerId, const rtc::Configuration& config)
                : id(peerId), connection(std::make_shared<rtc::PeerConnection>(config))
            {
            }

            void Fail()
            {
                {
                    std::lock_guard lock(mutex);
                    failed = true;
                }
                gathering.notify_all();
            }
        };

        rtc::Configuration PeerConfiguration(const FRtcServerOptions& options)
        {
            rtc::Configuration config;
            config.mtu = 1200;
            config.maxMessageSize = FRtcFraming::maxFrameBytes;
            if (options.port != 0)
            {
                config.enableIceUdpMux = true;
                config.portRangeBegin = config.portRangeEnd = options.port;
            }
            if (!options.stun.empty()) config.iceServers.emplace_back(options.stun);
            return config;
        }

        class FWebRtcTransport final : public INetTransport
        {
        public:
            FWebRtcTransport(FRtcServerOptions options, size_t capacity)
                : options_(std::move(options)), capacity_(capacity)
            {
            }

            ~FWebRtcTransport() override
            {
                masterThread_.request_stop();
                if (http_) http_->stop();
                if (httpThread_.joinable()) httpThread_.join();
                if (masterThread_.joinable()) masterThread_.join();
                for (auto& [id, peer] : peers_) peer->connection->close();
            }

            bool StartServer(std::string& error)
            {
                listener_ = std::make_unique<rtc::IceUdpMuxListener>(options_.port);
                if (options_.signalPort != 0)
                {
                    http_ = std::make_unique<httplib::Server>();
                    http_->set_payload_max_length(maxSignalBytes);
                    http_->set_read_timeout(8);
                    http_->set_write_timeout(8);
                    http_->Post("/rtc/offer", [this](const httplib::Request& request, httplib::Response& response)
                    {
                        RtcJson reply;
                        try
                        {
                            const auto offer = RtcJson::parse(request.body).at("sdp").get<std::string>();
                            reply["sdp"] = AcceptOffer(offer);
                        }
                        catch (const std::exception& cause)
                        {
                            response.status = 400;
                            reply["error"] = cause.what();
                        }
                        response.set_content(reply.dump(), "application/json");
                    });
                    if (!options_.webRoot.empty())
                    {
                        http_->set_file_extension_and_mimetype_mapping("wasm", "application/wasm");
                        http_->set_file_extension_and_mimetype_mapping("mjs", "text/javascript");
                        if (!http_->set_mount_point("/", options_.webRoot))
                        {
                            error = "cannot open local WebRTC preview directory";
                            return false;
                        }
                    }
                    if (!http_->bind_to_port("127.0.0.1", options_.signalPort))
                    {
                        error = fmt::format("cannot listen for WebRTC signaling on TCP {}", options_.signalPort);
                        return false;
                    }
                    httpThread_ = std::jthread([this] { http_->listen_after_bind(); });
                    http_->wait_until_ready();
                }
                if (!options_.masterUrl.empty())
                {
                    masterThread_ = std::jthread([this](std::stop_token stop) { PumpMaster(stop); });
                }
                SPDLOG_INFO("[NextNet][net] WebRTC listening on UDP {} (local signaling TCP {})",
                            options_.port, options_.signalPort);
                return true;
            }

            void StartClient(const std::string& url)
            {
                clientHttp_ = std::make_unique<FHttpWorker>();
                clientHttp_->SetBaseUrl(url);
                const auto peer = AddPeer();
                Attach(peer, peer->connection->createDataChannel("r"));
                rtc::Reliability unreliable;
                unreliable.unordered = true;
                unreliable.maxRetransmits = 0;
                rtc::DataChannelInit init;
                init.reliability = unreliable;
                Attach(peer, peer->connection->createDataChannel("u", init));
            }

            void Poll(std::vector<FNetEvent>& events) override
            {
                if (clientHttp_) clientHttp_->Pump();

                std::vector<std::shared_ptr<FRtcPeer>> active;
                {
                    std::lock_guard lock(peersMutex_);
                    active.reserve(peers_.size());
                    for (const auto& [id, peer] : peers_) active.push_back(peer);
                }
                for (const auto& peer : active)
                {
                    if (!PumpPeer(peer, events))
                    {
                        peer->connection->close();
                        {
                            std::lock_guard lock(peersMutex_);
                            peers_.erase(peer->id);
                        }
                        events.push_back({FNetEvent::EType::Disconnected, peer->id, {}});
                    }
                }
            }

            bool IsConnected(FPeerId id) const override
            {
                const auto peer = Find(id);
                return peer && peer->connected && peer->closeAt == RtcClock::time_point{};
            }

            bool Send(FPeerId id, std::span<const std::byte> payload, EDelivery delivery) override
            {
                const auto peer = Find(id);
                if (!peer || !peer->connected || peer->closeAt != RtcClock::time_point{}) return false;
                const auto& channel = delivery == EDelivery::Reliable ? peer->reliable : peer->unreliable;
                try
                {
                    if (channel->bufferedAmount() >= maxQueuedBytes)
                    {
                        if (delivery == EDelivery::Reliable) peer->Fail();
                        return false;
                    }
                    return peer->framing.Write(payload, delivery, channel->maxMessageSize(),
                        [&channel](std::span<const std::byte> frame)
                        {
                            if (!channel->isOpen()) return false;
                            channel->send(frame.data(), frame.size());
                            return true; // false from libdatachannel means buffered, not refused
                        });
                }
                catch (const std::exception&)
                {
                    peer->Fail();
                    return false;
                }
            }

            void Disconnect(FPeerId id) override
            {
                if (const auto peer = Find(id); peer && peer->closeAt == RtcClock::time_point{})
                {
                    if (!Control(*peer, ERtcFrame::Close, 0)) peer->Fail();
                    peer->closeAt = RtcClock::now() + closeTimeout;
                }
            }

            FPeerStats Stats(FPeerId id) const override
            {
                const auto peer = Find(id);
                return peer ? FPeerStats{peer->roundTripMs, 0.0f} : FPeerStats{};
            }

        private:
            std::shared_ptr<FRtcPeer> Find(FPeerId id) const
            {
                std::lock_guard lock(peersMutex_);
                const auto it = peers_.find(id);
                return it == peers_.end() ? nullptr : it->second;
            }

            std::shared_ptr<FRtcPeer> AddPeer()
            {
                std::lock_guard lock(peersMutex_);
                if (peers_.size() >= capacity_) throw std::runtime_error("WebRTC peer limit reached");
                const auto peer = std::make_shared<FRtcPeer>(nextPeer_++, PeerConfiguration(options_));
                const auto weak = std::weak_ptr(peer);
                peer->connection->onGatheringStateChange([weak](rtc::PeerConnection::GatheringState state)
                {
                    if (state != rtc::PeerConnection::GatheringState::Complete) return;
                    if (const auto owner = weak.lock())
                    {
                        {
                            std::lock_guard lock(owner->mutex);
                            owner->gathered = true;
                        }
                        owner->gathering.notify_all();
                    }
                });
                peer->connection->onStateChange([weak](rtc::PeerConnection::State state)
                {
                    if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed ||
                        state == rtc::PeerConnection::State::Disconnected)
                    {
                        if (const auto owner = weak.lock()) owner->Fail();
                    }
                });
                peer->connection->onDataChannel([weak](std::shared_ptr<rtc::DataChannel> channel)
                {
                    if (const auto owner = weak.lock())
                    {
                        std::lock_guard lock(owner->mutex);
                        if (owner->arrivingChannels.size() >= 2) owner->failed = true;
                        else owner->arrivingChannels.push_back(std::move(channel));
                    }
                });
                peers_.emplace(peer->id, peer);
                return peer;
            }

            static void Attach(const std::shared_ptr<FRtcPeer>& peer, std::shared_ptr<rtc::DataChannel> channel)
            {
                const auto properties = channel->reliability();
                const bool ordered = channel->label() == "r" && !properties.unordered &&
                                     !properties.maxRetransmits && !properties.maxPacketLifeTime;
                const bool unordered = channel->label() == "u" && properties.unordered && properties.maxRetransmits == 0;
                if ((!ordered && !unordered) || (ordered && peer->reliable) || (unordered && peer->unreliable))
                {
                    peer->Fail();
                    return;
                }
                const auto delivery = ordered ? EDelivery::Reliable : EDelivery::Unreliable;
                const auto weak = std::weak_ptr(peer);
                channel->onMessage([weak, delivery](rtc::message_variant message)
                {
                    if (const auto owner = weak.lock())
                    {
                        auto* bytes = std::get_if<rtc::binary>(&message);
                        std::lock_guard lock(owner->mutex);
                        if (!bytes || bytes->empty() || bytes->size() > FRtcFraming::maxFrameBytes ||
                            bytes->size() > maxQueuedBytes - owner->queuedBytes || owner->arrivingFrames.size() >= 8192)
                        {
                            owner->failed = true;
                            return;
                        }
                        owner->queuedBytes += bytes->size();
                        owner->arrivingFrames.push_back({delivery, std::move(*bytes)});
                    }
                });
                channel->onClosed([weak] { if (const auto owner = weak.lock()) owner->Fail(); });
                (ordered ? peer->reliable : peer->unreliable) = std::move(channel);
            }

            std::string AcceptOffer(const std::string& offer)
            {
                if (offer.empty() || offer.size() > maxSignalBytes) throw std::runtime_error("invalid WebRTC offer size");
                const auto peer = AddPeer();
                try
                {
                    peer->connection->setRemoteDescription(rtc::Description(offer, "offer"));
                    {
                        std::unique_lock lock(peer->mutex);
                        if (!peer->gathering.wait_for(lock, gatherTimeout, [&] { return peer->gathered || peer->failed; }) ||
                            peer->failed)
                        {
                            throw std::runtime_error("WebRTC ICE gather did not complete");
                        }
                    }
                    auto answer = peer->connection->localDescription();
                    if (!answer) throw std::runtime_error("WebRTC answer unavailable");
                    if (!options_.publicHost.empty() && options_.publicHost != "127.0.0.1" &&
                        options_.publicHost != "localhost" && options_.publicHost != "::1")
                    {
                        rtc::Candidate candidate(fmt::format("candidate:nextnet 1 UDP 2130706431 {} {} typ host",
                                                            options_.publicHost, options_.port));
                        if (candidate.resolve(rtc::Candidate::ResolveMode::Lookup)) answer->addCandidate(candidate);
                    }
                    return answer->generateSdp();
                }
                catch (...)
                {
                    peer->Fail();
                    throw;
                }
            }

            void PumpMaster(std::stop_token stop)
            {
                RtcJson identity{{"id", options_.serverId}, {"key", options_.serverKey}};
                while (!stop.stop_requested())
                {
                    const auto job = HttpPostJson(options_.masterUrl, "/ds/signal", identity.dump(), {}, 4);
                    if (stop.stop_requested()) return;
                    if (job.status == 204) continue;
                    if (!job.Ok())
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(250));
                        continue;
                    }
                    auto reply = identity;
                    try
                    {
                        const auto request = RtcJson::parse(job.body);
                        reply["request"] = request.at("request");
                        reply["sdp"] = AcceptOffer(request.at("sdp"));
                    }
                    catch (const std::exception& error)
                    {
                        if (!reply.contains("request")) continue;
                        reply["error"] = error.what();
                    }
                    HttpPostJson(options_.masterUrl, "/ds/signal", reply.dump(), {}, 4);
                }
            }

            void SignalClient(const std::shared_ptr<FRtcPeer>& peer)
            {
                offerSent_ = true;
                const auto description = peer->connection->localDescription();
                if (!description)
                {
                    peer->Fail();
                    return;
                }
                clientHttp_->Post("/rtc/offer", RtcJson{{"sdp", description->generateSdp()}}.dump(),
                    [weak = std::weak_ptr(peer)](const FHttpResult& response)
                    {
                        const auto owner = weak.lock();
                        if (!owner) return;
                        try
                        {
                            if (!response.Ok()) throw std::runtime_error(response.error);
                            const auto sdp = RtcJson::parse(response.body).at("sdp").get<std::string>();
                            owner->connection->setRemoteDescription(rtc::Description(sdp, "answer"));
                        }
                        catch (const std::exception& cause)
                        {
                            SPDLOG_WARN("[NextNet][net] WebRTC signaling: {}", cause.what());
                            owner->Fail();
                        }
                    }, {}, 10);
            }

            static bool Control(FRtcPeer& peer, ERtcFrame kind, uint32_t id)
            {
                const auto& channel = peer.reliable;
                if (!channel || !channel->isOpen()) return false;
                FNetWriter writer;
                writer.Write(kind).Write(id);
                try
                {
                    channel->send(writer.Bytes().data(), writer.Size());
                    return true;
                }
                catch (const std::exception&)
                {
                    return false;
                }
            }

            static bool Receive(FRtcPeer& peer, FRtcIncoming& frame, std::vector<FNetEvent>& events,
                                RtcClock::time_point now)
            {
                if (frame.delivery == EDelivery::Reliable && frame.bytes[0] >= static_cast<std::byte>(ERtcFrame::Ping))
                {
                    FNetReader reader(frame.bytes);
                    const auto kind = reader.Read<ERtcFrame>();
                    const auto id = reader.Read<uint32_t>();
                    if (reader.Failed() || !reader.AtEnd()) return false;
                    switch (kind)
                    {
                    case ERtcFrame::Ping: return Control(peer, ERtcFrame::Pong, id);
                    case ERtcFrame::Pong:
                        if (id == peer.pingId && peer.pingSent != RtcClock::time_point{})
                        {
                            const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(now - peer.pingSent);
                            peer.roundTripMs = static_cast<uint32_t>(time.count());
                        }
                        return true;
                    case ERtcFrame::Close:
                        peer.closeAt = now + closeTimeout;
                        return Control(peer, ERtcFrame::CloseAck, id);
                    case ERtcFrame::CloseAck:
                        return peer.closeAt == RtcClock::time_point{};
                    default: return false;
                    }
                }

                std::vector<std::byte> payload;
                const auto result = peer.framing.Read(frame.bytes, frame.delivery, payload);
                if (result == ERtcPayload::Ready) events.push_back({FNetEvent::EType::Message, peer.id, std::move(payload)});
                return result != ERtcPayload::Invalid;
            }

            bool PumpPeer(const std::shared_ptr<FRtcPeer>& peer, std::vector<FNetEvent>& events)
            {
                const auto now = RtcClock::now();
                std::vector<std::shared_ptr<rtc::DataChannel>> arrivals;
                bool gathered;
                {
                    std::lock_guard lock(peer->mutex);
                    if (peer->failed) return false;
                    gathered = peer->gathered;
                    arrivals.swap(peer->arrivingChannels);
                }
                for (auto& channel : arrivals) Attach(peer, std::move(channel));
                if (clientHttp_ && !offerSent_ && gathered) SignalClient(peer);
                if (!peer->connected)
                {
                    if (peer->reliable && peer->unreliable && peer->reliable->isOpen() && peer->unreliable->isOpen())
                    {
                        peer->connected = true;
                        events.push_back({FNetEvent::EType::Connected, peer->id, {}});
                    }
                    else
                    {
                        return now - peer->created < connectionTimeout;
                    }
                }
                if (peer->closeAt != RtcClock::time_point{} && now >= peer->closeAt) return false;
                std::vector<FRtcIncoming> incoming;
                {
                    std::lock_guard lock(peer->mutex);
                    incoming.swap(peer->arrivingFrames);
                    peer->queuedBytes = 0;
                }
                for (auto& frame : incoming)
                {
                    if (!Receive(*peer, frame, events, now)) return false;
                }
                if (peer->closeAt == RtcClock::time_point{} && now - peer->pingSent >= std::chrono::seconds(1))
                {
                    peer->pingSent = now;
                    if (!Control(*peer, ERtcFrame::Ping, ++peer->pingId)) return false;
                }
                return true;
            }

            FRtcServerOptions options_;
            size_t capacity_;
            mutable std::mutex peersMutex_;
            std::unique_ptr<rtc::IceUdpMuxListener> listener_;
            std::unordered_map<FPeerId, std::shared_ptr<FRtcPeer>> peers_;
            FPeerId nextPeer_ = 1;
            bool offerSent_ = false;
            std::unique_ptr<FHttpWorker> clientHttp_;
            std::unique_ptr<httplib::Server> http_;
            std::jthread httpThread_;
            std::jthread masterThread_;
        };
    }
#endif

    std::unique_ptr<INetTransport> ListenWebRtc(const FRtcServerOptions& options, size_t maxPeers, std::string& error)
    {
#if GK_NEXTNET_RTC
        try
        {
            auto transport = std::make_unique<FWebRtcTransport>(options, maxPeers);
            if (transport->StartServer(error)) return transport;
        }
        catch (const std::exception& cause)
        {
            error = fmt::format("WebRTC listener: {}", cause.what());
        }
#else
        error = "WebRTC support is unavailable in this build";
#endif
        return {};
    }

    std::unique_ptr<INetTransport> ConnectWebRtc(const std::string& signalUrl, std::string& error)
    {
#if GK_NEXTNET_RTC
        try
        {
            if (signalUrl.empty()) throw std::runtime_error("missing WebRTC signaling URL");
            FRtcServerOptions options;
            options.stun = RtcEnv("GK_NEXTNET_RTC_STUN");
            auto transport = std::make_unique<FWebRtcTransport>(std::move(options), 1);
            transport->StartClient(signalUrl);
            return transport;
        }
        catch (const std::exception& cause)
        {
            error = fmt::format("WebRTC client: {}", cause.what());
        }
#else
        error = "WebRTC support is unavailable in this build";
#endif
        return {};
    }
}
