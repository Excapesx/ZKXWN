// rmtc_bot9/llm.hpp —— 统一 AI 网关的异步客户端
//
// 约束（对应通信协议第 5 节）：
//   * 只有 HTTP 2xx 才消耗配额，失败不消耗 -> 可以对 429/502/504/连接错误做有限重试；
//   * 8.0：客户端读超时（transport 把 status 置 -1）不重试——网关可能已经成功并扣了配额，
//     7.0 用 2.5 s 超时并重试 3 次，慢模型下每个公告最多白扣 3 次配额却一个结果都拿不到；
//   * 网关不重试上游，重试由调用方负责；
//   * 不能在动作窗里等模型 —— 全部请求在线程里做，主线程只读结果。
#pragma once

#include <atomic>
#include <functional>
#include <utility>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace rmtc_bot9 {

using HttpTransport=std::function<bool(const std::string&,const std::string&,const std::string&,int,
                                       std::string*,long*,const std::atomic<bool>&)>;
bool http_post(const std::string& endpoint,const std::string& token,const std::string& body,int timeoutMs,
               std::string* response,long* status,const std::atomic<bool>& stopping);

struct LlmResult {
    std::string content;
    int sourceTick = 0;
    std::string session;
    bool valid = false;
    bool httpSuccess = false;
    long httpStatus = 0;  // -1: client timeout; 0: other transport error
    int elapsedMs = 0;
};

class LlmWorker {
public:
    explicit LlmWorker(HttpTransport transport) : transport_(std::move(transport)) {}
    ~LlmWorker();

    // model must be empty; maxTokens=0 uses 512, negative values are rejected.
    void configure(const std::string& endpoint, const std::string& token, const std::string& model,
                   int maxTokens, int timeoutMs);
    void start();
    void reset_session(const std::string& session);
    void stop();

    // 提交一次请求（同一时刻只保留最新一条，避免堆积）
    void submit(const std::string& prompt, int tick, const std::string& session);

    // 取回尚未消费的结果
    bool poll(LlmResult* out);

    std::string status() const;
    int successes() const { return successes_.load(); }
    // 本场 HTTP 2xx 次数（含空正文）：每次都消耗网关配额，用于节流
    int http_successes() const { return httpSuccesses_.load(); }
    bool hard_blocked() const { return blocked_.load(); }
    bool enabled() const { return enabled_; }

private:
    void loop();
    bool post(const std::string& body, std::string* response, long* status);

    HttpTransport transport_;
    bool enabled_ = false;
    std::string endpoint_;
    std::string token_;
    int maxTokens_ = 512;
    int timeoutMs_ = 15000;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> cancelled_{false};
    std::string activeSession_;
    bool pending_ = false;
    std::string pendingPrompt_;
    int pendingTick_ = 0;
    std::string pendingSession_;
    std::optional<LlmResult> result_;
    std::string status_ = "idle";
    std::thread worker_;
    std::atomic<int> successes_{0};
    std::atomic<int> httpSuccesses_{0};
    std::atomic<bool> blocked_{false};
};

}  // namespace rmtc_bot9
