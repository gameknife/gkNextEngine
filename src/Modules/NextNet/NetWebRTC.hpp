#pragma once

#include "Modules/NextNet/NetTransport.hpp"

namespace NextNet
{
    struct FRtcServerOptions
    {
        uint16_t port = 0;
        uint16_t signalPort = 0; // loopback HTTP for development direct-connect
        std::string webRoot;
        std::string masterUrl;
        std::string serverId;
        std::string serverKey;
        std::string publicHost;
        std::string stun;
    };

    bool ReadWebRtcServerOptions(FRtcServerOptions& options, std::string& error);
    bool WebRtcAvailable();
    std::unique_ptr<INetTransport> ListenWebRtc(const FRtcServerOptions& options, size_t maxPeers,
                                               std::string& error);
    std::unique_ptr<INetTransport> ConnectWebRtc(const std::string& signalUrl, std::string& error);
}
