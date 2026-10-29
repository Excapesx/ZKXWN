#!/usr/bin/env bash
# 在"练习包几何"的第二阶段地图上跑离线 A/B 测评。
#
# 为什么需要这个脚本：
#   官方练习包的 stage2 是 18x18 对称地图（4 角落出生点、每象限一个高价值资源、
#   到达任务在正中）。而 scripts/bench.sh 用的是 24x24 随机墙地图。两者几何完全不同，
#   在随机地图上得到的分数不能代表正式地图。下面的脚本按练习包结构生成多张同构地图，
#   再用同一套离线裁判（tools/bench/referee.cpp）跑 seat 比较。
#
# 用法:
#   python3 scripts/gen_practice_maps.py /tmp/pmaps 64
#   bash scripts/bench_practice.sh --a 10.1 --b 9.1 --maps 64
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
MAPS_DIR="${MAPS_DIR:-/tmp/opencode/pmaps}"
COUNT="${COUNT:-64}"
SEED="${SEED:-20261028}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --maps) COUNT="$2"; shift 2 ;;
    --seed) SEED="$2"; shift 2 ;;
    --dir)  MAPS_DIR="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [[ ! -f "$MAPS_DIR/stage2_000.json" ]]; then
  echo "generating $COUNT practice-geometry maps into $MAPS_DIR"
  python3 "$ROOT/scripts/gen_practice_maps.py" "$MAPS_DIR" "$COUNT" "$SEED"
fi

cd "$ROOT"
cmake -S . -B build_arena -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build_arena --parallel 8 --target rmtc_arena >/dev/null

OUT="$(mktemp)"
for i in $(seq 0 $((COUNT - 1))); do
  F=$(printf "%s/stage2_%03d.json" "$MAPS_DIR" "$i")
  ./build_arena/rmtc_arena --stage 2 --practice "$F" --a 10.1 --b 9.1 --field 111 \
    --events standard --maps 1 --output "$OUT.one" >/dev/null
  cat "$OUT.one" >> "$OUT"
done
rm -f "$OUT.one"

python3 - "$OUT" <<'PY'
import json, statistics, sys
rows = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
a = [r["a"] for r in rows]
n = len(a)
def m(k): return sum(x[k] for x in a) / n
def pct(p):
    s = sorted(x["score"] for x in a)
    return s[int(round((len(s) - 1) * p))]
caps = sum(1 for x in a if x["damage"] >= 500) / n
print("=== practice-geometry stage 2, %d games ===" % n)
print("  score      mean %.3f  median %.1f  min %.1f  p05 %.1f  max %.1f" %
      (m("score"), statistics.median([x["score"] for x in a]), min(x["score"] for x in a),
       pct(.05), max(x["score"] for x in a)))
print("  resource   %.2f / 120   damage %.1f / 500 (capped %.1f%%)" % (m("resource"), m("damage"), caps * 100))
print("  missions   %.3f  (normal %.3f  high %.3f  reach %.3f)" %
      (m("missions"), m("mission_normal"), m("mission_high"), m("mission_reach")))
print("  rank1 %d  top2 %d  / %d   invalid %d" %
      (sum(1 for x in a if x["rank"] == 1), sum(1 for x in a if x["rank"] <= 2), n,
       sum(x["invalid"] for x in a)))
PY
rm -f "$OUT"
