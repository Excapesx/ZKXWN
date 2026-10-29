// rmtc2/parse.hpp —— 感知解析 / 动作序列化 / 模型计划校验
#pragma once

#include <string>

#include "rmtc2/types.hpp"

namespace rmtc2 {

// 解析一帧感知。容忍未知实体类型（跳过而不是整帧丢弃），
// 但关键字段（tick/session/map/self）缺失时返回 false。
bool parse_perception(const std::string& text, Perception* out, std::string* err);

// 生成动作 JSON，按协议回填 tick 与 session_id。
std::string action_json(const Action& action, int tick, const std::string& session);

// 把模型返回的推理文本（可能带 markdown 围栏）整理成 Plan。
// 只做结构与范围校验，不做关键词匹配。
bool parse_plan(const std::string& content, const std::string& session, int sourceTick,
                int maxTicks, int mapW, int mapH, Plan* out);

// 从任意文本中取出第一个括号配平的 JSON 对象
std::string first_json_object(const std::string& text);

}  // namespace rmtc2
