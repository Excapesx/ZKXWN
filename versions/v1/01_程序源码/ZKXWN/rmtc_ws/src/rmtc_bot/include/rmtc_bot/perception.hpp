// OWNER: A
#pragma once
#include "rmtc_bot/types.hpp"

namespace rmtc_bot {
Vec2 parse_relative_position(const std::string& text);
Perception parse_perception(const std::string& text);
std::string action_to_json(const Action& action, std::int64_t tick, const std::string& session_id);
bool action_allowed(const Action& action, const Perception& perception, int stage);
} // namespace rmtc_bot

