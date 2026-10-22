#!/usr/bin/env python3
"""Summarize local and practice-referee multimap results without hidden maps."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--local', type=Path, default=ROOT / 'build/multimap/final.json')
    p.add_argument('--referee', type=Path, default=ROOT / 'build/multimap-referee-final/report.json')
    p.add_argument('--output', type=Path, default=ROOT / 'docs/multimap-results.json')
    args = p.parse_args()
    local = json.loads(args.local.read_text())
    referee = json.loads(args.referee.read_text())
    compact = [{k: v for k, v in c.items() if k != 'trace'} for c in local['cases']]
    randoms = [c for c in compact if c['name'].startswith('random_')]
    summary = dict(local_map_count=len(compact), local_integrity_failures=local['failed'],
                   random_map_count=len(randoms), random_mean_value=statistics.mean(c['value'] for c in randoms),
                   random_full_collection_count=sum(c['value'] == c['total_value'] for c in randoms),
                   referee_map_count=len(referee['cases']), referee_passed_count=sum(c['passed'] for c in referee['cases']))
    files = ['rmtc_ws/src/rmtc_bot/test/maps/stage1_suite.json',
             'rmtc_ws/src/rmtc_bot/src/memory.cpp', 'rmtc_ws/src/rmtc_bot/src/planner.cpp',
             'rmtc_ws/src/rmtc_bot/src/ros_io.cpp', 'decision/src/engine.cpp']
    summary['sha256'] = {f: hashlib.sha256((ROOT / f).read_bytes()).hexdigest() for f in files}
    report = dict(summary=summary, local_cases=compact, referee=referee)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(summary, ensure_ascii=False, indent=2))

if __name__ == '__main__':
    main()
