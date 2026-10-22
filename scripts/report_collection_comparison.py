#!/usr/bin/env python3
"""Summarize paired collection tests; values are resources, not official scores."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[1]

def compare(before, after):
    left = json.loads(before.read_text())
    right = json.loads(after.read_text())
    rows = []
    for a, b in zip(left['cases'], right['cases']):
        assert a['name'] == b['name']
        if a['name'].startswith('random_'):
            rows.append(dict(name=a['name'], before=a['value'], after=b['value'], delta=b['value']-a['value'],
                             minimum=b['minimum_value'], passed=b['pass']))
    assert len(left['cases']) == len(right['cases'])
    return dict(map_count=len(rows), mean_before=statistics.mean(r['before'] for r in rows),
                mean_after=statistics.mean(r['after'] for r in rows),
                improved=sum(r['delta'] > 0 for r in rows), regressed=sum(r['delta'] < 0 for r in rows),
                unchanged=sum(r['delta'] == 0 for r in rows),
                threshold_failures_before=left['failed'], threshold_failures_after=right['failed'],
                integrity_errors=sum(c[k] for c in right['cases'] for k in
                                     ('invalid_actions', 'missing_actions', 'bad_context', 'receipt_errors')),
                max_callback_us=max(c['callback_us_max'] for c in right['cases']), cases=rows)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--artifacts', type=Path, default=ROOT / 'build/collection-upgrade')
    p.add_argument('--output', type=Path, default=ROOT / 'docs/collection-comparison.json')
    args = p.parse_args()
    root = args.artifacts
    report = dict(
        training=compare(ROOT / 'build/multimap/final.json', root / 'training-after.json'),
        development=compare(root / 'validation-before.json', root / 'validation-after.json'),
        held_out=compare(root / 'holdout-before.json', root / 'holdout-after.json'))
    source = ROOT / 'rmtc_ws/src/rmtc_bot/src/policies/stage1_policy.cpp'
    report['policy_sha256'] = hashlib.sha256(source.read_bytes()).hexdigest()
    referee = root / 'referee/report.json'
    if referee.exists():
        report['practice_referee'] = json.loads(referee.read_text())
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    for name in ('training', 'development', 'held_out'):
        r = report[name]
        print(f"{name}: {r['mean_before']} -> {r['mean_after']}; improved={r['improved']} regressed={r['regressed']}")

if __name__ == '__main__':
    main()
