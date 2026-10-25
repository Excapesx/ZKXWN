// SHARED
#pragma once
#include "rmtc_bot/types.hpp"

namespace rmtc_bot {
struct Config {
    std::string team = "A", team_name = "RMTC local bot";
    std::string token;
    std::string gateway = "http://127.0.0.1:8080/v1/chat/completions";
    std::string model;
    std::string decision_type = "balanced";
    int stage = 1, max_ticks = 100;
    int request_timeout_ms = 2500;
    int max_tokens = 0; // 0 omits the field locally; use organizer's limit for formal runs.
    bool llm_enabled = false;
};
Config load_config_from_env();
void validate_config(const Config& config);
rmtc::decision::Mode decision_mode(const std::string& name);
} // namespace rmtc_bot

