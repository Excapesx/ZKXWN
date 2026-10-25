#!/usr/bin/env python3
"""Re-run the declared frozen v6 comparison matrix; Python standard library only."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--arena",required=True,type=Path,help="Compiled rmtc_arena binary")
    parser.add_argument("--out",required=True,type=Path,help="New output directory (must not exist)")
    args=parser.parse_args()
    exe=args.arena.resolve()
    if not exe.is_file():parser.error("arena does not exist")
    out=args.out.resolve()
    if out.exists():parser.error("choose a new output directory to preserve existing evidence")
    out.mkdir(parents=True)
    root=Path(__file__).resolve().parents[1]
    sources={}
    for folder in ["src","tools/bench"]:
        for f in sorted((root/folder).rglob("*")):
            if f.is_file():sources[f.relative_to(root).as_posix()]=hashlib.sha256(f.read_bytes()).hexdigest()
    (out/"frozen_sources.json").write_text(json.dumps(sources,ensure_ascii=False,indent=2),encoding="utf-8")
    cases=[
        ("four_vs4",4,4,100,83000,24,24,4,"g",12,2),
        ("four_vs5",5,4,100,83000,24,24,4,"g",12,2),
        ("duel_vs4",4,2,200,91000,24,24,4,"g",2,2),
        ("duel_vs5",5,2,200,91000,24,24,4,"g",2,2),
        ("strong_vs4",4,4,40,99100,24,24,4,"4",12,2),
        ("strong_vs5",5,4,40,99100,24,24,4,"5",12,2),
        ("rectangle_vs4",4,4,40,123000,32,16,2,"g",12,2),
        ("rectangle_vs5",5,4,40,123000,32,16,2,"g",12,2),
        ("stage1_vs4",4,2,100,151000,20,20,4,"g",2,1),
        ("stage1_vs5",5,2,100,151000,20,20,4,"g",2,1),
        ("symmetry4",4,4,12,171000,24,24,4,"g",12,2),
        ("symmetry5",5,4,12,171000,24,24,4,"g",12,2),
        ("symmetry6",6,4,12,171000,24,24,4,"g",12,2),
    ]
    results={}
    for name,opponent,robots,maps,seed,w,h,vision,other,rotations,stage in cases:
        a=opponent if name.startswith("symmetry") else 6
        cmd=[str(exe),"--a",str(a),"--b",str(opponent),"--robots",str(robots),"--matches",str(maps),
             "--seed",str(seed),"--width",str(w),"--height",str(h),"--vision",str(vision),
             "--others",other,"--rotations",str(rotations),"--stage",str(stage),
             "--output",str(out/(name+".jsonl"))]
        start=time.monotonic()
        process=subprocess.run(cmd,capture_output=True,text=True,encoding="utf-8",timeout=300)
        (out/(name+".stdout.json")).write_text(process.stdout,encoding="utf-8")
        if process.returncode:raise RuntimeError(name+": "+process.stderr)
        data=json.loads(process.stdout)
        data["wall_seconds"]=time.monotonic()-start
        data["command"]=cmd
        results[name]=data
        (out/"summary.json").write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding="utf-8")
        if name.startswith("symmetry") and abs(data["mean_difference"])>1e-8:raise RuntimeError("symmetry failure")
        if data["all_policy_invalid_total"]:raise RuntimeError("invalid actions found")
        print(name, "games",data["games"],"mean difference",round(data["mean_difference"],4),flush=True)
    for name,sha in sources.items():
        if hashlib.sha256((root/name).read_bytes()).hexdigest()!=sha:raise RuntimeError("source changed: "+name)

if __name__=="__main__":
    main()

