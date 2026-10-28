// rmtc101/announce.hpp —— 公告累积与模型请求节流（纯 C++，无 ROS 依赖，便于测试）
//
// 7.0 的做法：感知里的 event_text 一变就发一次请求，结果回来时若期间又发过新请求
// 就当“过时”丢掉；公告是滚动窗口，频繁变化时可能一直拿不到计划。另外只把当前
// 窗口里的几行发给模型，早先的公告滚出窗口后模型就看不到了。
//
// 8.0：
//   * AnnouncementLog accumulates accepted perception.event_text for this session.
//     Unscoped retained topic strings are not admitted by the 9.0 node.
//     This class only deduplicates lines; it never interprets announcement meaning.
//   * PlanScheduler 决定什么时候发计划请求：有新公告行才发；两次计划请求之间的
//     最小间隔随剩余配额自动放大，保证整场不把配额用光。
#pragma once

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace rmtc101 {

class AnnouncementLog {
public:
    void reset() {
        lines_.clear();
        seen_.clear();
        version_ = 0;
    }
    // 按换行拆分、去掉首尾空白、按完全相同的文本去重；返回新加入的行数。
    int ingest(const std::string& text) {
        int added = 0;
        std::size_t start = 0;
        while (start <= text.size()) {
            std::size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(start, end - start);
            const auto first = line.find_first_not_of(" \t\r");
            const auto last = line.find_last_not_of(" \t\r");
            line = first == std::string::npos ? std::string{} : line.substr(first, last - first + 1);
            if (!line.empty() && line.size() <= 4096 && seen_.insert(line).second) {
                lines_.push_back(line);
                ++added;
                if (lines_.size() > kMaxLines) {  // 整场上限，超出时丢最早的（集合保留，防止反复加入）
                    lines_.erase(lines_.begin());
                }
            }
            if (end == text.size()) break;
            start = end + 1;
        }
        if (added > 0) ++version_;
        return added;
    }
    const std::vector<std::string>& lines() const { return lines_; }
    // 最近的若干行（总字节数受限），按原顺序
    std::vector<std::string> recent(std::size_t maxLines, std::size_t maxBytes) const {
        std::vector<std::string> out;
        std::size_t bytes = 0;
        for (auto it = lines_.rbegin(); it != lines_.rend() && out.size() < maxLines; ++it) {
            if (bytes + it->size() > maxBytes) break;
            bytes += it->size();
            out.push_back(*it);
        }
        std::reverse(out.begin(), out.end());
        return out;
    }
    unsigned version() const { return version_; }

private:
    static constexpr std::size_t kMaxLines = 512;
    std::vector<std::string> lines_;
    std::set<std::string> seen_;
    unsigned version_ = 0;
};

struct PlanScheduler {
    int quotaBudget = 90;   // 本场最多花在计划上的成功调用数（配额暂定 100，留 10 次余量）
    int minInterval = 3;    // 两次计划请求至少隔 3 Tick

    // 计划请求的最小间隔：剩余 Tick / 剩余预算，不小于 minInterval
    int interval(int tick, int maxTicks, int httpSuccesses) const {
        const int left = std::max(1, quotaBudget - httpSuccesses);
        return std::max(minInterval, (maxTicks - tick) / left);
    }
    bool want_plan(bool hasNew, int tick, int lastPlanTick, int maxTicks, int httpSuccesses) const {
        if (!hasNew) return false;
        if (httpSuccesses >= quotaBudget) return false;
        return tick - lastPlanTick >= interval(tick, maxTicks, httpSuccesses);
    }
};

}  // namespace rmtc101
