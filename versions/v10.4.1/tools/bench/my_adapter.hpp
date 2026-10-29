#pragma once

#include <memory>

#include "opponent_adapter.hpp"

// 我方历史版本（仅用于离线对抗测评）
std::unique_ptr<IPolicy> make_my2_bot(int stage, int maxTicks);  // 校内2.0 / rmtc2
std::unique_ptr<IPolicy> make_my5_bot(int stage, int maxTicks);  // 校内5.0 / rmtc3
