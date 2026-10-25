#pragma once

#include <memory>
#include <string>

#include "rmtc8/types.hpp"

struct IPolicy {
    virtual ~IPolicy() = default;
    virtual rmtc8::Action act(const rmtc8::Perception& p) = 0;
    // 离线 oracle 交付的“模型回答”。各版本用自己的 parse_plan 解析，解析失败就当没收到。
    virtual bool plan(const std::string& content, const std::string& session, int tick, int maxTicks, int w, int h) {
        (void)content; (void)session; (void)tick; (void)maxTicks; (void)w; (void)h;
        return false;
    }
    virtual const char* name() const = 0;
};

std::unique_ptr<IPolicy> make_v7_bot(int stage, int maxTicks);
