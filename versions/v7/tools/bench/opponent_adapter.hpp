#pragma once

#include <memory>

#include "rmtc7/types.hpp"

struct IPolicy {
    virtual ~IPolicy() = default;
    virtual rmtc7::Action act(const rmtc7::Perception& p) = 0;
    virtual const char* name() const = 0;
};

std::unique_ptr<IPolicy> make_v6_bot(int stage, int maxTicks);
