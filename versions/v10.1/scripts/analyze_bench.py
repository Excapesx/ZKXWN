#!/usr/bin/env python3
"""Summarize rmtc_arena per-game JSONL without changing the strategy.

Usage:
  python3 scripts/analyze_bench.py results.jsonl
  python3 scripts/analyze_bench.py results.jsonl --json summary.json
"""
import argparse
import json
import statistics
from collections import Counter


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("--json", default="")
    ap.add_argument("--low", type=int, default=12)
    args = ap.parse_args()

    rows = []
    with open(args.input, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    if not rows:
        raise SystemExit("no rows")

    # A is the candidate. Stage-2 rows also have B and seat; stage-1 rows only A.
    a = [r["a"] for r in rows]
    scores = [x["score"] for x in a]
    n = len(a)

    def pct(p):
        s = sorted(scores)
        return s[min(len(s) - 1, max(0, int(round((len(s) - 1) * p))))]

    summary = {
        "games": n,
        "maps": len({r.get("map", i) for i, r in enumerate(rows)}),
        "score": {
            "mean": round(sum(scores) / n, 6),
            "median": statistics.median(scores),
            "min": min(scores),
            "max": max(scores),
            "p05": pct(0.05),
            "p25": pct(0.25),
            "p75": pct(0.75),
            "p95": pct(0.95),
            "below_38": sum(1 for x in scores if x < 38),
            "below_36": sum(1 for x in scores if x < 36),
        },
        "mean_resource": round(sum(x["resource"] for x in a) / n, 6),
        "mean_damage": round(sum(x["damage"] for x in a) / n, 6),
        "mean_missions": round(sum(x["missions"] for x in a) / n, 6),
        # Stage 1 has no missions; zero mission fields are not failures there.
        "mission_failures": None if all(x.get("missions", 0) == 0 for x in a) else {
            "normal": sum(1 for x in a if not x.get("mission_normal", 1)),
            "high": sum(1 for x in a if not x.get("mission_high", 1)),
            "reach": sum(1 for x in a if not x.get("mission_reach", 1)),
        },
        "mean_deaths": round(sum(x.get("deaths", 0) for x in a) / n, 6),
        "mean_waits": round(sum(x.get("waits", 0) for x in a) / n, 6),
        "invalid_total": sum(x.get("invalid", 0) for x in a),
        "low_games": [],
    }

    # Keep the low tail visible: these are the games that pull the mean down.
    indexed = list(enumerate(rows))
    indexed.sort(key=lambda pair: pair[1]["a"]["score"])
    for idx, r in indexed[: args.low]:
        x = r["a"]
        summary["low_games"].append({
            "map": r.get("map", idx),
            "seat": r.get("seat"),
            "score": x["score"],
            "resource": x["resource"],
            "damage": x["damage"],
            "missions": x["missions"],
            "mission_high": x.get("mission_high"),
            "mission_normal": x.get("mission_normal"),
            "mission_reach": x.get("mission_reach"),
            "deaths": x.get("deaths"),
            "waits": x.get("waits"),
        })

    print(json.dumps(summary, ensure_ascii=False, indent=2))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(summary, f, ensure_ascii=False, indent=2)
            f.write("\n")


if __name__ == "__main__":
    main()
