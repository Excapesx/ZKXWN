#pragma once

#include <memory>

#include "rmtc6/types.hpp"

struct IPolicy {
    virtual ~IPolicy() = default;
    virtual rmtc6::Action act(const rmtc6::Perception& p) = 0;
    virtual const char* name() const = 0;
};

// 对手：直接编译 ../校内4.0 的 rmtc2::Bot，不改其决策逻辑。
std::unique_ptr<IPolicy> make_v4_bot(int stage, int maxTicks);

std::unique_ptr<IPolicy> make_v5_bot(int stage, int maxTicks);
