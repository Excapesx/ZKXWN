#!/usr/bin/env python3
"""Replay public historical frames. Does NOT simulate counterfactual outcomes."""
import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, default=ROOT / 'build_core')
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    fixture = ROOT / '历史9.2说明/evidence/recorded_stage2'
    observations = (fixture / 'public_perceptions.jsonl').read_bytes()
    recorded = [json.loads(line) for line in (fixture / 'recorded_actions.jsonl').read_text(encoding='utf-8').splitlines()]
    result = {'scope': 'Historical-observation policy replay; NOT a counterfactual match/score evaluation'}
    for version, name in [('9.1', 'rmtc_replay91'), ('10.1', 'rmtc_replay')]:
        choices = [args.build/name, args.build/(name+'.exe'), args.build/'Release'/(name+'.exe')]
        binary = next((x for x in choices if x.is_file()), None)
        if binary is None:
            raise RuntimeError('Build the replay tools first: '+name)
        run = subprocess.run([str(binary.resolve()), '2'], input=observations, capture_output=True, timeout=60)
        if run.returncode:
            raise RuntimeError('Replay failed: '+version)
        rows = [json.loads(line) for line in run.stdout.splitlines()]
        if len(rows)!=501 or any('rejected' in r for r in rows):
            raise RuntimeError('Missing/rejected recorded frame')
        mismatches = sum(row['action']!=old for row, old in zip(rows[:500], recorded))
        tail = rows[297:500]
        result[version] = {'frames':len(rows),'different_from_recorded_actions':mismatches,
                           'tail_297_to_499_actions':dict(Counter(r['action']['action'] for r in tail)),
                           'tail_reasons':dict(Counter(r['reason'] for r in tail)),
                           'frontiers_at_297':rows[297]['frontier_count']}
    result['ok'] = result['9.1']['different_from_recorded_actions']==0 and result['10.1']['tail_297_to_499_actions'].get('wait',0)<203
    text=json.dumps(result,ensure_ascii=False,indent=2)+'\n'
    if args.output: args.output.write_text(text,encoding='utf-8')
    print(text,end='')
    return 0 if result['ok'] else 1

if __name__=='__main__':
    sys.exit(main())
