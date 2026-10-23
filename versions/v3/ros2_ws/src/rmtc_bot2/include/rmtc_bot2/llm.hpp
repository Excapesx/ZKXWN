// rmtc_bot2/llm.hpp —— 统一 AI 网关的异步客户端
//
// 约束（对应通信协议第 5 节）：
//   * 只有 HTTP 2xx 才消耗配额，失败不消耗 -> 可以对 429/502/504/连接错误做有限重试；
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

namespace rmtc_bot2 {

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
};

class LlmWorker {
public:
    explicit LlmWorker(HttpTransport transport) : transport_(std::move(transport)) {}
    ~LlmWorker();

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
    bool hard_blocked() const { return blocked_.load(); }
    bool enabled() const { return enabled_; }

private:
    void loop();
    bool post(const std::string& body, std::string* response, long* status);

    HttpTransport transport_;
    bool enabled_ = false;
    std::string endpoint_;
    std::string token_;
    std::string model_;
    int maxTokens_ = 0;
    int timeoutMs_ = 2500;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stopping_{false};
    std::string activeSession_;
    bool pending_ = false;
    std::string pendingPrompt_;
    int pendingTick_ = 0;
    std::string pendingSession_;
    std::optional<LlmResult> result_;
    std::string status_ = "idle";
    std::thread worker_;
    std::atomic<int> successes_{0};
    std::atomic<bool> blocked_{false};
};

}  // namespace rmtc_bot2
