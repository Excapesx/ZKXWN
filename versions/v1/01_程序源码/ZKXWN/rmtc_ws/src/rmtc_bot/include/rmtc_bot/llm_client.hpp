// OWNER: B
#pragma once
#include "rmtc_bot/config.hpp"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace rmtc_bot {
struct LLMRequest {
    std::string session_id, prompt;
    int tick = 0, width = 0, height = 0, max_ticks = 0;
};
struct HttpReply { long status = 0; std::string body; };
class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    virtual HttpReply post(const Config&, const std::string& body, const std::atomic<bool>& stopping) = 0;
};
std::shared_ptr<HttpTransport> make_curl_transport();
std::optional<LLMPlan> parse_llm_response(const std::string& body, const LLMRequest& request);
LLMRequest make_llm_request(const Perception& p, int max_ticks);

class LLMClient {
public:
    LLMClient(Config config, std::shared_ptr<HttpTransport> transport);
    ~LLMClient();
    LLMClient(const LLMClient&) = delete;
    LLMClient& operator=(const LLMClient&) = delete;
    void reset_session(const std::string& session_id);
    void submit(LLMRequest request);
    std::optional<LLMPlan> latest() const;
    std::string status() const;
    int http_successes() const;
    void stop();
private:
    void work();
    Config config_;
    std::shared_ptr<HttpTransport> transport_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    std::optional<LLMRequest> pending_;
    std::optional<LLMPlan> latest_;
    std::string session_, status_ = "disabled";
    std::uint64_t epoch_ = 0;
    int http_successes_ = 0;
    bool blocked_ = false;
};
} // namespace rmtc_bot

