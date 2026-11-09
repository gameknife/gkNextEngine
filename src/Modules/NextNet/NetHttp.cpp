#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextNet/NetHttp.hpp"

#if GK_NEXTNET_HTTP
#include <httplib.h>
#endif

#include <chrono>

namespace NextNet
{
    bool HttpAvailable()
    {
#if GK_NEXTNET_HTTP
        return true;
#else
        return false;
#endif
    }

    FHttpResult HttpPostJson(const std::string& baseUrl, const std::string& path, const std::string& json,
                             const std::string& bearer, int timeoutSeconds)
    {
        FHttpResult result;
#if GK_NEXTNET_HTTP
        httplib::Client client(baseUrl);
        client.set_connection_timeout(timeoutSeconds, 0);
        client.set_read_timeout(timeoutSeconds, 0);
        client.set_write_timeout(timeoutSeconds, 0);
        httplib::Headers headers;
        if (!bearer.empty()) headers.emplace("Authorization", "Bearer " + bearer);
        const httplib::Result response = client.Post(path, headers, json, "application/json");
        if (!response)
        {
            result.error = fmt::format("{}{}: {}", baseUrl, path, httplib::to_string(response.error()));
            return result;
        }
        result.status = response->status;
        result.body = response->body;
        if (!result.Ok())
        {
            // The Master answers {"error": "..."}; keep it readable without a JSON parser here.
            const size_t key = result.body.find("\"error\"");
            const size_t open = key == std::string::npos ? key : result.body.find('"', result.body.find(':', key) + 1);
            const size_t close = open == std::string::npos ? open : result.body.find('"', open + 1);
            result.error = close == std::string::npos ? fmt::format("HTTP {}", result.status)
                                                      : result.body.substr(open + 1, close - open - 1);
        }
#else
        (void)baseUrl; (void)path; (void)json; (void)bearer; (void)timeoutSeconds;
        result.error = "this build has no HTTP client";
#endif
        return result;
    }

    FHttpWorker::~FHttpWorker()
    {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    void FHttpWorker::Post(std::string path, std::string json, FDone done, std::string bearer, int timeoutSeconds)
    {
        {
            std::lock_guard lock(mutex_);
            queue_.push_back({std::move(path), std::move(json), std::move(bearer), timeoutSeconds, std::move(done)});
            if (!thread_.joinable()) thread_ = std::thread([this] { Run(); });
        }
        wake_.notify_one();
    }

    void FHttpWorker::Run()
    {
        for (;;)
        {
            FJob job;
            std::string baseUrl;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (stopping_ && queue_.empty()) return;
                job = std::move(queue_.front());
                queue_.pop_front();
                ++inFlight_;
                baseUrl = baseUrl_;
            }
            FHttpResult result = HttpPostJson(baseUrl, job.path, job.json, job.bearer, job.timeoutSeconds);
            std::lock_guard lock(mutex_);
            finished_.emplace_back(std::move(job.done), std::move(result));
            --inFlight_;
        }
    }

    void FHttpWorker::Pump()
    {
        std::deque<std::pair<FDone, FHttpResult>> finished;
        {
            std::lock_guard lock(mutex_);
            finished.swap(finished_);
        }
        for (auto& [done, result] : finished)
        {
            if (done) done(result);
        }
    }

    size_t FHttpWorker::Pending() const
    {
        std::lock_guard lock(mutex_);
        return queue_.size() + inFlight_ + finished_.size();
    }

    void FHttpWorker::Drain(double seconds)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
        while (std::chrono::steady_clock::now() < deadline)
        {
            Pump();
            {
                std::lock_guard lock(mutex_);
                if (queue_.empty() && inFlight_ == 0 && finished_.empty()) return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Pump();
    }
}
