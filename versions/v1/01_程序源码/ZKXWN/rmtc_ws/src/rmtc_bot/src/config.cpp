// SHARED
#include "rmtc_bot/config.hpp"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace rmtc_bot {
namespace {
std::string env(const char* key, const std::string& fallback = {}) {
    const char* value = std::getenv(key);
    return value && *value ? value : fallback;
}
int number(const char* key, int fallback) {
    const auto value = env(key);
    if (value.empty()) return fallback;
    std::size_t used = 0;
    const int result = std::stoi(value, &used);
    if (used != value.size()) throw std::runtime_error(std::string("Invalid integer setting: ") + key);
    return result;
}
}
rmtc::decision::Mode decision_mode(const std::string& value) {
    if (value == "balanced") return rmtc::decision::Mode::TaskFocus;
    if (value == "fight") return rmtc::decision::Mode::Aggressive;
    if (value == "collect") return rmtc::decision::Mode::Survival;
    throw std::runtime_error("decision_type must be balanced, fight or collect");
}
void validate_config(const Config& c) {
    if (c.team.size() != 1 || c.team[0] < 'A' || c.team[0] > 'D') throw std::runtime_error("team must be A/B/C/D");
    if (c.stage != 1 && c.stage != 2) throw std::runtime_error("stage must be explicitly 1 or 2");
    if (c.max_ticks < 1 || c.max_ticks > 100000) throw std::runtime_error("invalid max_ticks");
    if (c.team_name.empty() || c.team_name.size() > 96 || c.team_name.find_first_of("\r\n") != std::string::npos)
        throw std::runtime_error("invalid team display name");
    decision_mode(c.decision_type);
    if (c.request_timeout_ms < 100 || c.request_timeout_ms > 30000) throw std::runtime_error("invalid HTTP timeout");
    if (c.max_tokens < 0 || c.max_tokens > 1000000) throw std::runtime_error("invalid model output limit");
    if (c.llm_enabled && (c.token.empty() || c.gateway.empty())) throw std::runtime_error("LLM requires configured gateway and token");
    if (c.gateway.find("http://") != 0 && c.gateway.find("https://") != 0) throw std::runtime_error("gateway must use HTTP/HTTPS");
    if (c.token.find_first_of("\r\n") != std::string::npos) throw std::runtime_error("invalid token header");
}
Config load_config_from_env() {
    Config c;
    c.team = env("RMTC_TEAM", c.team);
    c.team_name = env("RMTC_TEAM_NAME", c.team_name);
    c.token = env("RMTC_TEAM_TOKEN");
    c.gateway = env("RMTC_AI_GATEWAY", c.gateway);
    c.model = env("RMTC_MODEL");
    c.stage = number("RMTC_STAGE", 1);
    c.max_ticks = number("RMTC_MAX_TICKS", c.stage == 1 ? 100 : 500);
    // decision_type is intentionally not read from an environment variable.
    // Use RMTC_DECISION_CONFIG pointing at decision_config.json.
    c.request_timeout_ms = number("RMTC_HTTP_TIMEOUT_MS", c.request_timeout_ms);
    c.max_tokens = number("RMTC_MAX_TOKENS", 0);
    c.llm_enabled = env("RMTC_LLM_ENABLED", "0") == "1";
    const auto file = env("RMTC_DECISION_CONFIG");
    if (!file.empty()) {
        std::ifstream stream(file);
        if (!stream) throw std::runtime_error("Cannot read registered decision config");
        const auto j = nlohmann::json::parse(stream);
        if (!j.is_object() || j.size() != 1 || !j.contains("decision_type") || !j["decision_type"].is_string())
            throw std::runtime_error("decision config may only contain decision_type");
        c.decision_type = j["decision_type"].get<std::string>();
    }
    validate_config(c);
    return c;
}
} // namespace rmtc_bot

