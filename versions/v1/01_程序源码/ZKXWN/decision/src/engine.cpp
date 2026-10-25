#include "rmtc_decision/engine.hpp"
#include "rmtc_decision/battle_evaluator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace rmtc::decision {
namespace {
constexpr std::array<Direction, 4> directions{
    Direction::Up, Direction::Right, Direction::Down, Direction::Left};

bool inside(Point p, const GameState& s) {
    return p.x >= 0 && p.y >= 0 && p.x < s.width && p.y < s.height;
}
long long distance(Point a, Point b) {
    return std::abs(static_cast<long long>(a.x) - b.x) +
           std::abs(static_cast<long long>(a.y) - b.y);
}
bool valid_direction(Direction d) {
    return std::find(directions.begin(), directions.end(), d) != directions.end();
}
bool finite_nonnegative(double value) { return std::isfinite(value) && value >= 0; }
bool valid(const GameState& s) {
    return !s.session_id.empty() && s.tick >= 0 && s.max_ticks > 0 &&
           s.width > 0 && s.height > 0 && inside(s.self.position, s) &&
           (s.stage == Stage::Collection || s.stage == Stage::Battle) &&
           finite_nonnegative(s.self.hp) && finite_nonnegative(s.score.resource_value) &&
           finite_nonnegative(s.score.effective_damage);
}
bool fresh(const Enemy& e, const GameState& s) {
    return e.observed_tick == s.tick && inside(e.position, s) &&
           std::isfinite(e.hp) && e.hp > 0;
}
bool completed(const Mission& m, const GameState& s) {
    return std::find(s.score.completed_mission_ids.begin(),
                     s.score.completed_mission_ids.end(), m.id) !=
           s.score.completed_mission_ids.end();
}
bool useful_mission(const Mission& m, const GameState& s) {
    return !m.id.empty() && std::isfinite(m.target) && m.target > 0 && !completed(m, s);
}
bool effect(const GameState& s, const std::optional<TickWindow> EffectSnapshot::*member) {
    if (!s.effects || !s.effects->validated || s.effects->session_id != s.session_id) return false;
    const auto& window = (*s.effects).*member;
    return window && window->contains(s.tick);
}
bool zone_contains(const DangerZone& z, Point p, const GameState& s) {
    if (!z.validated || z.session_id != s.session_id || z.window.start < 0 ||
        z.window.end <= z.window.start) return false;
    if (z.excludes_initial_spawn && s.self.initial_spawn && p == *s.self.initial_spawn) return false;
    if (z.shape == DangerZone::Shape::Cross) return p.x == z.first.x || p.y == z.first.y;
    return p.x >= z.first.x && p.x <= z.last.x && p.y >= z.first.y && p.y <= z.last.y;
}
double environment_damage(const GameState& s, Point p, int tick, bool before, bool protection) {
    double damage = 0;
    for (const auto& z : s.dangers) {
        if (z.before_movement != before || !z.window.contains(tick) || !zone_contains(z,p,s) ||
            (protection && z.respects_protection)) continue;
        if (z.instant_death) damage += s.self.hp;
        else if (finite_nonnegative(z.damage)) damage += z.damage;
    }
    return damage;
}
double future_environment_risk(const GameState& s, Point p, bool protection) {
    double risk = 0;
    for (const auto& z : s.dangers) {
        if (z.window.end <= s.tick || !zone_contains(z,p,s) || (protection && z.respects_protection)) continue;
        const int delay = std::max(1, z.window.start - s.tick);
        const double advisory = finite_nonnegative(z.advisory_risk) ? z.advisory_risk : 0;
        const double harm = z.instant_death ? 200 : (finite_nonnegative(z.damage) ? z.damage : 0);
        risk += std::max(harm,advisory) / delay;
    }
    return risk;
}
bool has(const std::vector<Point>& list, Point p) {
    return std::find(list.begin(), list.end(), p) != list.end();
}
std::vector<Point> blockers(const GameState& s) {
    std::vector<Point> result = s.known_walls;
    for (const auto& b : s.temporary_blocks) {
        if (b.session_id == s.session_id && b.window.contains(s.tick)) result.push_back(b.position);
    }
    for (const auto& e : s.enemies) if (fresh(e, s)) result.push_back(e.position);
    return result;
}
std::optional<Direction> ray(Point from, Point to) {
    if (from == to) return std::nullopt;
    if (from.x == to.x) return to.y < from.y ? Direction::Up : Direction::Down;
    if (from.y == to.y) return to.x < from.x ? Direction::Left : Direction::Right;
    return std::nullopt;
}
const Enemy* target_in_direction(const GameState& s, Direction dir) {
    const Enemy* target = nullptr;
    int best = 3;
    for (const auto& e : s.enemies) {
        const auto direction = ray(s.self.position, e.position);
        const auto d = distance(s.self.position, e.position);
        if (fresh(e, s) && direction && *direction == dir && d < best) {
            target = &e;
            best = static_cast<int>(d);
        }
    }
    return target; // Includes protected vehicles, which screen more distant targets.
}
double incoming(const GameState& s, Point position, bool defending, bool protected_spawn) {
    if (s.stage != Stage::Battle || protected_spawn) return 0;
    double result = 0;
    for (const auto& attacker : s.enemies) {
        if (!fresh(attacker, s) || attacker.protected_spawn) continue;
        const auto dir = ray(attacker.position, position);
        const auto d = distance(attacker.position, position);
        if (!dir || d > 2) continue;
        bool screened = false;
        for (const auto& other : s.enemies) {
            if (&other == &attacker || !fresh(other, s)) continue;
            const auto other_dir = ray(attacker.position, other.position);
            if (other_dir && *other_dir == *dir && distance(attacker.position, other.position) < d) {
                screened = true;
                break;
            }
        }
        // Conservative estimate: every visible eligible enemy chooses to fire.
        if (!screened) result += defending ? 10 : 20;
    }
    if (effect(s, &EffectSnapshot::vulnerability)) result *= 2;
    return result;
}
double progress_bonus(double progress, double target, double added) {
    if (!finite_nonnegative(progress) || target <= progress) return 0;
    return 5 * std::min(added, target - progress) / (target - progress);
}
double resource_reward(const Resource& r, const GameState& s) {
    if (s.stage == Stage::Collection) return r.value;
    double reward = 0.25 * std::min(r.value, std::max(0.0, 120 - s.score.resource_value));
    reward += 0.001 * r.value; // Small tie-break value after the resource score cap.
    for (const auto& m : s.missions) {
        if (!useful_mission(m, s)) continue;
        if (m.kind == MissionKind::ResourceValue) {
            reward += progress_bonus(s.score.resource_value, m.target, r.value);
        } else if ((m.kind == MissionKind::HighResources && r.value >= 20) ||
                   (m.kind == MissionKind::NormalResources && r.value < 20)) {
            // Progress is an estimate; actual completion always comes from referee IDs.
            if (m.observed_progress) reward += progress_bonus(*m.observed_progress, m.target, 1);
            else reward += 5 / std::max(1.0, m.target);
        }
    }
    return reward;
}
double damage_mission_reward(double projected, const GameState& s) {
    double reward = 0;
    for (const auto& m : s.missions) {
        if (useful_mission(m, s) && m.kind == MissionKind::EffectiveDamage)
            reward += progress_bonus(s.score.effective_damage, m.target,
                                     std::max(0.0, projected-s.score.effective_damage));
    }
    return reward;
}
double damage_reward(double damage, const GameState& s) {
    // v1.1 section 10.2: 15 points at adjusted damage 500. The input already
    // includes referee kill rewards and death deductions; never adjust it here.
    double reward = (15.0 / 500.0) * std::min(damage, std::max(0.0, 500 - s.score.effective_damage));
    reward += 0.0005 * damage;
    reward += damage_mission_reward(s.score.effective_damage+damage, s);
    return reward;
}
BattleAward estimate_attack_award(const GameState& s, const Enemy& victim, double nominal) {
    const std::string own = "__self";
    std::vector<DamageClaim> claims{{own,nominal,true}};
    const auto solo = resolve_damage_batch(victim.hp,claims).awards.at(own);
    for (const auto& attacker : s.enemies) {
        if (&attacker == &victim || !fresh(attacker,s) || attacker.protected_spawn) continue;
        const auto dir = ray(attacker.position,victim.position);
        const auto range = distance(attacker.position,victim.position);
        if (!dir || range > 2) continue;
        bool screened = false;
        const auto blocks = [&](Point p) {
            const auto d = ray(attacker.position,p);
            return d && *d == *dir && distance(attacker.position,p) < range;
        };
        if (blocks(s.self.position)) screened = true;
        for (const auto& other : s.enemies)
            if (&other != &attacker && &other != &victim && fresh(other,s) && blocks(other.position)) screened = true;
        if (!screened) claims.push_back({"enemy:"+attacker.id,nominal,true});
    }
    const auto contested = resolve_damage_batch(victim.hp,claims).awards.at(own);
    // Compare solo and all-visible-competitor scenarios conservatively. Incoming
    // risk remains a separate upper bound, not a joint opponent-action prediction.
    return contested.attack_credit+contested.kill_credit < solo.attack_credit+solo.kill_credit
        ? contested : solo;
}
struct Weights {
    double attack = 1;
    double current_damage = 0.06;
    double future_damage = 0.04;
    double death = 100;
};
Weights weights(Mode mode) {
    if (mode == Mode::Aggressive) return {1.8, 0.03, 0.015, 60};
    if (mode == Mode::Survival) return {0.45, 0.10, 0.09, 140};
    return {};
}
struct Candidate {
    Action action;
    Point expected;
    std::optional<Point> goal;
    std::string key;
    std::string reason;
    double reward = 0;
    double self_reflection = 0;
    BattleAward attack_award;
};
} // namespace

Point step(Point p, Direction dir) {
    switch (dir) {
    case Direction::Up: --p.y; break;
    case Direction::Right: ++p.x; break;
    case Direction::Down: ++p.y; break;
    case Direction::Left: --p.x; break;
    }
    return p;
}
Direction opposite(Direction dir) {
    switch (dir) {
    case Direction::Up: return Direction::Down;
    case Direction::Right: return Direction::Left;
    case Direction::Down: return Direction::Up;
    case Direction::Left: return Direction::Right;
    }
    return Direction::Up;
}
const char* name(Direction dir) {
    switch (dir) {
    case Direction::Up: return "up";
    case Direction::Right: return "right";
    case Direction::Down: return "down";
    case Direction::Left: return "left";
    }
    return "invalid";
}
const char* name(ActionKind kind) {
    switch (kind) {
    case ActionKind::Wait: return "wait";
    case ActionKind::Move: return "move";
    case ActionKind::Attack: return "attack";
    case ActionKind::Defend: return "defend";
    }
    return "invalid";
}

DecisionEngine::DecisionEngine(const Planner& planner, Mode mode) : planner_(planner), mode_(mode) {}
void DecisionEngine::reset() {
    session_.clear();
    retired_sessions_.clear();
    last_tick_ = -1;
    cached_.reset();
    previous_goal_key_.clear();
}

Decision DecisionEngine::decide(const GameState& s) {
    Decision result;
    if (!valid(s)) { result.reason = "invalid_state"; return result; }
    if (s.session_id != session_) {
        if (std::find(retired_sessions_.begin(), retired_sessions_.end(), s.session_id) != retired_sessions_.end()) {
            result.reason = "retired_session";
            return result;
        }
        if (!session_.empty()) retired_sessions_.push_back(session_);
        session_ = s.session_id;
        last_tick_ = -1;
        cached_.reset();
        previous_goal_key_.clear();
    }
    if (s.tick >= s.max_ticks) { result.reason = "finished"; return result; }
    if (s.tick < last_tick_) { result.reason = "stale_tick"; return result; }
    if (s.tick == last_tick_ && cached_) {
        if (s.stage != last_stage_) { result.reason = "inconsistent_stage"; return result; }
        result = *cached_;
        result.reused_tick = true;
        return result;
    }

    const auto save = [&](Decision value, const std::string& key) {
        last_tick_ = s.tick;
        last_stage_ = s.stage;
        previous_goal_key_ = key;
        cached_ = value;
        return value;
    };
    result.submit = true;
    result.expected_position = s.self.position;
    if (!s.self.alive || s.self.hp <= 0) {
        result.reason = "dead_wait";
        return save(result, "");
    }
    if (s.stage == Stage::Battle && effect(s, &EffectSnapshot::forced_wait)) {
        result.reason = "event_forced_wait";
        return save(result, "");
    }

    const auto blocked = blockers(s);
    const auto w = weights(mode_);
    std::vector<Candidate> candidates;
    candidates.push_back({{}, s.self.position, {}, "", "no_beneficial_action", 0, 0});
    const double base_incoming = incoming(s, s.self.position, false, s.self.protected_spawn);
    if (s.stage == Stage::Battle && base_incoming > 0)
        candidates.push_back({{ActionKind::Defend, {}}, s.self.position, {}, "", "defend_threat", -0.01, 0});

    const auto add_goal = [&](Point target, double reward, const std::string& key,
                              const std::string& reason) {
        if (!inside(target, s) || !std::isfinite(reward) || reward <= 0 ||
            (has(blocked, target) && target != s.self.position)) return;
        std::optional<Route> route;
        try { route = planner_.plan({s.self.position, target, s.width, s.height, blocked}); }
        catch (const std::exception&) { return; }
        if (!route || route->distance < distance(s.self.position, target) ||
            route->distance > s.max_ticks - s.tick) return;
        Candidate c;
        c.goal = target;
        c.expected = s.self.position;
        c.key = key;
        c.reason = reason;
        if (target != s.self.position) {
            if (route->distance <= 0) return;
            if (route->wait_first) {
                if (route->first_step) return;
            } else {
                if (!route->first_step || !valid_direction(*route->first_step)) return;
                c.expected = step(s.self.position, *route->first_step);
                if (!inside(c.expected, s) || has(blocked, c.expected)) return;
                auto command = *route->first_step;
                if (s.stage == Stage::Battle && effect(s, &EffectSnapshot::inverted_movement)) command = opposite(command);
                c.action = {ActionKind::Move, command};
            }
        } else if (route->distance != 0) return;
        c.reward = reward / (route->distance + 1.0);
        if (key == previous_goal_key_) c.reward += 0.05; // Small target hysteresis.
        candidates.push_back(std::move(c));
    };

    for (const auto& r : s.resources) {
        if (!std::isfinite(r.value) || r.value <= 0) continue;
        const auto key = "resource:" + r.id + ":" + std::to_string(r.position.x) + ":" + std::to_string(r.position.y);
        add_goal(r.position, resource_reward(r, s), key, "resource");
    }
    if (s.stage == Stage::Battle) {
        if (s.advisory_target && s.advisory_priority >= 1 && s.advisory_priority <= 3)
            add_goal(*s.advisory_target, 1.5*s.advisory_priority, "model_advice", "model_advice");
        for (const auto& m : s.missions) {
            if (m.kind == MissionKind::Reach && useful_mission(m, s) && m.position)
                add_goal(*m.position, 5, "mission:" + m.id, "task_reach");
        }
        if (!s.self.protected_spawn) {
            for (const auto dir : directions) {
                const auto* enemy = target_in_direction(s, dir);
                if (!enemy || enemy->protected_spawn) continue;
                double damage = std::min(enemy->hp, enemy->defending ? 10.0 : 20.0);
                double reflection = 0;
                if (enemy->defending && effect(s, &EffectSnapshot::reflection)) {
                    damage = 0;
                    reflection = finite_nonnegative(s.effects->reflection_damage)
                        ? s.effects->reflection_damage : s.self.hp;
                }
                if (damage <= 0) continue;
                const auto award = estimate_attack_award(s,*enemy,enemy->defending ? 10.0 : 20.0);
                candidates.push_back({{ActionKind::Attack, dir}, s.self.position, {}, "",
                                      award.kill_credit > 0 ? "attack_finish_estimate" : "attack_score",
                                      0, reflection, award});
            }
        }
        if (mode_ == Mode::Aggressive && !s.self.protected_spawn) {
            for (const auto& e : s.enemies) {
                if (!fresh(e, s) || e.protected_spawn || distance(s.self.position, e.position) > 6) continue;
                for (const auto dir : directions) {
                    Point target = e.position;
                    for (int n = 1; n <= 2; ++n) {
                        target = step(target, dir);
                        if (target == s.self.position) continue;
                        add_goal(target, 0.8 * w.attack * damage_reward(std::min(20.0, e.hp), s),
                                 "pursue:" + e.id, "approach_enemy");
                    }
                }
            }
        }
    }
    for (const auto p : s.exploration_targets) {
        if (p != s.self.position)
            add_goal(p, 0.7, "explore:" + std::to_string(p.x) + ":" + std::to_string(p.y), "explore");
    }
    if (s.stage == Stage::Battle && (base_incoming > 0 ||
        future_environment_risk(s,s.self.position,s.self.protected_spawn) > 0)) {
        for (auto dir : directions) {
            const auto p = step(s.self.position, dir);
            if (!inside(p, s) || has(blocked, p)) continue;
            auto command = effect(s, &EffectSnapshot::inverted_movement) ? opposite(dir) : dir;
            candidates.push_back({{ActionKind::Move, command}, p, {}, "", "reduce_future_threat", -0.005, 0});
        }
    }

    const Candidate* best = nullptr;
    double best_utility = -std::numeric_limits<double>::infinity();
    for (const auto& c : candidates) {
        // Current attacks resolve BEFORE our movement; use the current position here.
        const double before_event = s.stage == Stage::Battle ?
            environment_damage(s,s.self.position,s.tick,true,s.self.protected_spawn) : 0;
        const double now = before_event + incoming(s, s.self.position, c.action.kind == ActionKind::Defend,
                                                  s.self.protected_spawn) + c.self_reflection;
        bool protected_later = s.self.protected_spawn;
        if (c.action.kind == ActionKind::Move &&
            (!s.self.initial_spawn || c.expected != *s.self.initial_spawn)) protected_later = false;
        const double after_event = s.stage == Stage::Battle ?
            environment_damage(s,c.expected,s.tick,false,protected_later) : 0;
        const bool fatal_now = now + after_event >= s.self.hp;
        // A destroyed vehicle cannot complete a later move or collect/reach a goal.
        // Its already-issued ordinary attack still resolves simultaneously.
        const bool can_score_attack = c.action.kind == ActionKind::Attack && before_event < s.self.hp;
        const double reward = fatal_now && !can_score_attack ? 0 : c.reward;
        const double later = fatal_now ? 0 : incoming(s, c.expected, false, protected_later) +
            (s.stage == Stage::Battle ? future_environment_risk(s,c.expected,protected_later) : 0);
        double utility = reward - w.current_damage * std::min(s.self.hp, now+after_event) - w.future_damage * later;
        if (s.stage == Stage::Battle) {
            const double current = s.score.effective_damage;
            const double attack = can_score_attack ? c.attack_award.attack_credit : 0;
            const double kills = can_score_attack ? c.attack_award.kill_credit : 0;
            const double before_penalty = forecast_adjusted_damage(current,attack,kills,0);
            const double projected = forecast_adjusted_damage(current,attack,kills,fatal_now ? 1 : 0);
            // Additions are mode-weighted; the official death score loss is not.
            if (can_score_attack) {
                utility += w.attack*(combat_points(before_penalty)-combat_points(current));
                utility += 0.0005*(attack+kills);
                if (!fatal_now) utility += damage_mission_reward(projected,s);
            }
            if (fatal_now) utility += combat_points(projected)-combat_points(before_penalty);
        }
        if (fatal_now) utility -= w.death;
        if (c.action.kind == ActionKind::Move) utility -= 0.002;
        if (utility > best_utility + 1e-9) { best = &c; best_utility = utility; }
    }
    result.action = best->action;
    result.goal = best->goal;
    result.expected_position = best->expected;
    result.reason = best->reason;
    result.utility = best_utility;
    return save(result, best->key);
}

} // namespace rmtc::decision

