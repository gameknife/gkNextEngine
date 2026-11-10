#pragma once

// ============================================================================
// NetHttp.hpp - JSON over HTTP to a service such as the NextDayz Master. A
// single background thread sends requests in the order they were queued (so a
// later checkpoint never overtakes an earlier one); results come back through
// Pump() on the thread that owns the worker. Built on cpp-httplib where it is
// available (Windows/Linux); elsewhere every request fails with an error.
// ============================================================================

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace NextNet
{
    struct FHttpResult
    {
        int status = 0;          // 0 when no response arrived
        std::string body;
        std::string error;       // transport failure, or the service's {"error": ...}
        bool Ok() const { return status >= 200 && status < 300; }
    };

    // Blocking POST of a JSON body to baseUrl + path (e.g. "http://127.0.0.1:7780", "/api/login").
    FHttpResult HttpPostJson(const std::string& baseUrl, const std::string& path, const std::string& json,
                             const std::string& bearer = {}, int timeoutSeconds = 10);
    bool HttpAvailable();

    class FHttpWorker
    {
    public:
        using FDone = std::function<void(const FHttpResult&)>;

        FHttpWorker() = default;
        ~FHttpWorker();
        FHttpWorker(const FHttpWorker&) = delete;
        FHttpWorker& operator=(const FHttpWorker&) = delete;

        void SetBaseUrl(std::string baseUrl) { baseUrl_ = std::move(baseUrl); }
        const std::string& BaseUrl() const { return baseUrl_; }
        void Post(std::string path, std::string json, FDone done, std::string bearer = {}, int timeoutSeconds = 10);
        // Runs the callbacks of finished requests, on the caller's thread.
        void Pump();
        size_t Pending() const;
        // Waits (up to `seconds`) for queued requests, then runs their callbacks.
        void Drain(double seconds);

    private:
        struct FJob
        {
            std::string path;
            std::string json;
            std::string bearer;
            int timeoutSeconds = 10;
            FDone done;
        };
        void Run();

        std::string baseUrl_;
        mutable std::mutex mutex_;
        std::condition_variable wake_;
        std::deque<FJob> queue_;
        std::deque<std::pair<FDone, FHttpResult>> finished_;
        size_t inFlight_ = 0;
        bool stopping_ = false;
        std::thread thread_;
    };
}
