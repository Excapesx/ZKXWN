#pragma once

#include <memory>
#include <string>

#include "rmtc9/types.hpp"

struct PolicyAudit {
    int switches=-1;
    bool detailed=false;
    std::string targetId;
    rmtc9::Point target;
};
struct IPolicy {
    virtual ~IPolicy() = default;
    virtual rmtc9::Action act(const rmtc9::Perception& p) = 0;
    // 离线 oracle 交付的“模型回答”。各版本用自己的 parse_plan 解析，解析失败就当没收到。
    virtual bool plan(const std::string& content, const std::string& session, int tick, int maxTicks, int w, int h) {
        (void)content; (void)session; (void)tick; (void)maxTicks; (void)w; (void)h;
        return false;
    }
    virtual const char* name() const = 0;
    virtual PolicyAudit audit() const { return {}; }
};

std::unique_ptr<IPolicy> make_v7_bot(int stage, int maxTicks);
std::unique_ptr<IPolicy> make_v8_bot(int stage, int maxTicks);
std::unique_ptr<IPolicy> make_v1011_bot(int stage, int maxTicks);

std::unique_ptr<IPolicy> make_v93_bot(int stage, int maxTicks);
std::unique_ptr<IPolicy> make_v100_bot(int stage, int maxTicks);
std::unique_ptr<IPolicy> make_v101_bot(int stage, int maxTicks);

std::unique_ptr<IPolicy> make_v102_bot(int stage,int ticks);

std::unique_ptr<IPolicy> make_v103_bot(int stage,int ticks);
