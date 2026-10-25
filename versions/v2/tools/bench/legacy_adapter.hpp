#pragma once

#include <memory>

#include "rmtc2/types.hpp"

struct IPolicy {
    virtual ~IPolicy() = default;
    virtual rmtc2::Action act(const rmtc2::Perception& p) = 0;
    virtual const char* name() const = 0;
};

std::unique_ptr<IPolicy> make_legacy_bot(int stage, int maxTicks, char seat);
