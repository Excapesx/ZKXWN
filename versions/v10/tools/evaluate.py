#!/usr/bin/env python3
"""重跑 8.0 的完整性能验证矩阵（只用 Python 标准库）。

  python3 tools/evaluate.py --arena build_arena/rmtc_arena --out evidence/rerun_01

* 输出目录必须是新目录，避免覆盖原始证据。
* 第二阶段一律用 seat 模式：同一张图、同一席位、同一批对手，8.0 与 7.0 各打一局再比分。
  种子 120000 起的地图在调参时从未用过（调参用 83000 起）。
* 官方练习包的 stage1.json / stage2.json 从 03_官方练习包_v1.1/dist.zip 里直接读出。
"""
import argparse
import hashlib
import io
import json
import subprocess
import tarfile
import time
import zipfile
from pathlib import Path

CASES = [
    # name, args
    ("stage1_20x20_v4_seed71000", "--stage 1 --maps 100 --seed 71000 --width 20 --height 20 --vision 4"),
    ("stage1_20x20_v4_seed90000", "--stage 1 --maps 200 --seed 90000 --width 20 --height 20 --vision 4"),
    ("stage1_20x20_v4_seed95000", "--stage 1 --maps 300 --seed 95000 --width 20 --height 20 --vision 4"),
    ("stage1_20x20_v3", "--stage 1 --maps 100 --seed 61000 --width 20 --height 20 --vision 3"),
    ("stage1_20x20_v2", "--stage 1 --maps 100 --seed 62000 --width 20 --height 20 --vision 2"),
    ("stage1_16x16_v4", "--stage 1 --maps 100 --seed 63000 --width 16 --height 16 --vision 4"),
    ("stage1_24x24_v4", "--stage 1 --maps 100 --seed 60000 --width 24 --height 24 --vision 4"),
    ("stage1_practice_map", "--stage 1 --maps 1 --seed 1 --practice {stage1}"),
    ("stage2_field_777", "--maps 100 --seed 120000 --field 777"),
    ("stage2_field_ggg", "--maps 100 --seed 120000 --field ggg"),
    ("stage2_field_rrr", "--maps 100 --seed 120000 --field rrr"),
    ("stage2_field_ccc", "--maps 100 --seed 120000 --field ccc"),
    ("stage2_field_www", "--maps 100 --seed 120000 --field www"),
    ("stage2_field_7g7", "--maps 100 --seed 120000 --field 7g7"),
    ("stage2_field_7rc", "--maps 100 --seed 120000 --field 7rc"),
    ("stage2_field_grw", "--maps 100 --seed 120000 --field grw"),
    ("stage2_field_888", "--maps 100 --seed 120000 --field 888"),
    ("stage2_1v1_vs7", "--maps 200 --seed 120000 --field 7 --robots 2"),
    ("stage2_events_standard", "--maps 100 --seed 120000 --field 777 --events standard"),
    ("stage2_events_practice", "--maps 100 --seed 120000 --field 777 --events practice"),
    ("stage2_events_llm_extra_hazards", "--maps 100 --seed 120000 --field 777 --events standard --noise 1"),
    ("stage2_events_llm_unknown_entry", "--maps 100 --seed 120000 --field 777 --events standard --noise 2"),
    ("stage2_events_llm_markdown", "--maps 100 --seed 120000 --field 777 --events standard --noise 3"),
    ("stage2_practice_map", "--maps 1 --seed 1 --field 777 --practice {stage2}"),
    ("stage2_practice_map_events", "--maps 1 --seed 1 --field 777 --practice {stage2} --events practice"),
    ("stage2_32x16_v2", "--maps 60 --seed 123000 --field 777 --width 32 --height 16 --vision 2"),
    ("stage2_duel_mode", "--mode duel --maps 50 --seed 120000 --field 777"),
    ("symmetry_8_vs_8", "--a 8 --b 8 --maps 50 --seed 171000 --field 777"),
]


def extract_practice(root: Path, out: Path):
    with zipfile.ZipFile(root / "03_官方练习包_v1.1" / "dist.zip") as z:
        name = next(n for n in z.namelist() if n.endswith("rmtc_practice.tar.gz"))
        data = z.read(name)
    paths = {}
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as t:
        for key in ("stage1", "stage2"):
            member = next(m for m in t.getmembers() if m.name.endswith("config/%s.json" % key))
            target = out / ("practice_%s.json" % key)
            target.write_bytes(t.extractfile(member).read())
            paths[key] = str(target)
    return paths


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--arena", required=True, type=Path, help="compiled rmtc_arena")
    parser.add_argument("--out", required=True, type=Path, help="new output directory (must not exist)")
    args = parser.parse_args()
    exe = args.arena.resolve()
    if not exe.is_file():
        parser.error("arena does not exist")
    out = args.out.resolve()
    if out.exists():
        parser.error("choose a new output directory to preserve existing evidence")
    out.mkdir(parents=True)
    root = Path(__file__).resolve().parents[1]
    sources = {}
    for folder in ["src", "baseline_v7/src", "tools/bench"]:
        for f in sorted((root / folder).rglob("*")):
            if f.is_file():
                sources[f.relative_to(root).as_posix()] = hashlib.sha256(f.read_bytes()).hexdigest()
    (out / "frozen_sources.json").write_text(json.dumps(sources, ensure_ascii=False, indent=2), encoding="utf-8")
    practice = extract_practice(root, out)
    results = {}
    for name, extra in CASES:
        cmd = [str(exe)] + extra.format(**practice).split() + ["--output", str(out / (name + ".jsonl"))]
        start = time.monotonic()
        process = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", timeout=3600)
        (out / (name + ".stdout.json")).write_text(process.stdout, encoding="utf-8")
        if process.returncode:
            raise RuntimeError(name + ": " + process.stderr)
        data = json.loads(process.stdout)
        data["wall_seconds"] = round(time.monotonic() - start, 1)
        data["command"] = ["rmtc_arena"] + [c if not c.startswith(str(out)) else Path(c).name for c in cmd[1:]]
        results[name] = data
        (out / "summary.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
        if name.startswith("symmetry") and abs(data["mean_difference"]) > 1e-9:
            raise RuntimeError("symmetry failure: identical programs must score identically")
        if data["ab_invalid_total"]:
            raise RuntimeError(name + ": invalid actions found")
        print("%-36s games %4d  8.0 %.2f  7.0 %.2f  diff %+.2f  95%% [%+.2f, %+.2f]" % (
            name, data["games"], data["a"]["score"], data["b"]["score"], data["mean_difference"],
            data["map_bootstrap_95"][0], data["map_bootstrap_95"][1]), flush=True)
    for name, sha in sources.items():
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != sha:
            raise RuntimeError("source changed during evaluation: " + name)


if __name__ == "__main__":
    main()
