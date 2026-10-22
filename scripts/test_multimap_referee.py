#!/usr/bin/env python3
"""Batch TEST driver for user-provided practice referee. Never imported by bot."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CASES = 'visible_detour,unseen_far_corner,sealed_high_resource,rectangular_bottom_right,narrow_view,long_corridor,zero_view_probe,random_03,random_31'

def final_settlement(run_dir):
    """Post-run tester audit only; never exposed to the contestant process."""
    score = json.loads((Path(run_dir) / 'score.json').read_text())
    row = next(a for a in score['actions'] if a['team'] == 'A')
    return dict(tick=score['action_tick'], source=row['source'], status=row['status'])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--practice-dir', type=Path, required=True)
    parser.add_argument('--suite', type=Path, default=ROOT / 'rmtc_ws/src/rmtc_bot/test/maps/stage1_suite.json')
    parser.add_argument('--cases', default=DEFAULT_CASES)
    parser.add_argument('--port', type=int, default=18089)
    parser.add_argument('--domain', type=int, default=195)
    parser.add_argument('--report-dir', type=Path, default=ROOT / 'build/multimap-referee')
    args = parser.parse_args()
    args.practice_dir = args.practice_dir.resolve()
    args.report_dir = args.report_dir.resolve()
    suite = json.loads(args.suite.read_text())
    selected = args.cases.split(',')
    by_name = {c['name']: c for c in suite['cases']}
    cases = [by_name[name] for name in selected]
    # Refuse an occupied port rather than controlling an unrelated referee.
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', args.port))
    args.report_dir.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith('RMTC_')}
    env.update(ROS_DOMAIN_ID=str(args.domain), ROS_LOCALHOST_ONLY='1', RMW_IMPLEMENTATION='rmw_fastrtps_cpp',
               RMTC_LLM_ENABLED='0', ROS_LOG_DIR=str(args.report_dir / 'ros-logs'))
    base = f'http://127.0.0.1:{args.port}'
    def api(path, body=None):
        req = urllib.request.Request(base + path, data=None if body is None else json.dumps(body).encode(),
                                     headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=5) as response:
            return json.load(response)
    def until(predicate, seconds):
        end = time.monotonic() + seconds
        last = None
        while time.monotonic() < end:
            try:
                last = predicate()
                if last:
                    return last
            except OSError:
                pass
            time.sleep(0.1)
        raise RuntimeError(f'Timed out after {seconds}s; last={last}')
    def ready():
        s = api('/api/state')
        if s.get('fault'):
            raise RuntimeError(s['fault'])
        return s if s.get('status', {}).get('ready') else None
    report = dict(kind='practice_referee_v1.1', tick_ms=100, max_ticks=100, cases=[])
    bot = referee = None
    botlog = open(args.report_dir / 'bot.log', 'w')
    reflog = open(args.report_dir / 'referee.log', 'w')
    try:
        referee = subprocess.Popen([str(args.practice_dir / 'start_referee.sh'), '--stage', '1',
                                    '--port', str(args.port), '--data-dir', str(args.report_dir / 'runs')],
                                   env=env, stdout=reflog, stderr=reflog)
        until(ready, 25)
        bot = subprocess.Popen([str(ROOT / 'rmtc_ws/install/rmtc_bot/lib/rmtc_bot/rmtc_bot'),
                                '--ros-args', '-p', 'team:=A', '-p', 'stage:=1', '-p', 'max_ticks:=100',
                                '-p', 'llm_enabled:=false', '--log-level', 'rmtc_bot:=debug'],
                               env=env, stdout=botlog, stderr=botlog)
        time.sleep(2)
        for case in cases:
            if bot.poll() is not None:
                raise RuntimeError('Bot exited')
            api('/api/start', dict(stage=1, tick_ms=100, max_ticks=100, bots=[], config=case['config']))
            until(ready, 25)
            # New referee processes need public DDS discovery before the first window.
            time.sleep(1)
            offset = (args.report_dir / 'bot.log').stat().st_size
            api('/api/resume', {})
            def finished():
                s = ready()
                return s if s and s['status'].get('finished') else None
            final = until(finished, 35)
            # Public diagnostic callbacks and scoring output settle asynchronously.
            time.sleep(0.3)
            final = api('/api/state')
            settlement = final_settlement(final['run_dir'])
            team = next(t for t in final['board']['teams'] if t['team'] == 'A')
            checks = final['checks'].get('A', {})
            with open(args.report_dir / 'bot.log', 'rb') as stream:
                stream.seek(offset)
                log_data = stream.read()
                times = sorted(int(t) for t in re.findall(rb'callback_us=(\d+)', log_data))
                published = [json.loads(t) for t in re.findall(rb'published_action=(\{[^\n]+\})', log_data)]
            minimum = case['minimum_value']
            ok = (not final['fault'] and checks.get('action_tick') == 99 and settlement['tick'] == 99 and settlement['source'] == 'submitted' and settlement['status'] != 'rejected'
                  and team['invalid_actions'] == 0 and (minimum is None or team['resource_value'] >= minimum))
            # Debug harness audits only this seat's public messages after the run.
            communications = Path(final['run_dir']) / 'communication.jsonl'
            actions, receipts = set(), set()
            if communications.exists():
                for line in communications.read_text().splitlines():
                    m = json.loads(line)
                    if m.get('seat') != 'A':
                        continue
                    data = m['data']
                    if m['kind'] == 'action':
                        actions.add(data['tick'])
                    elif m['kind'] == 'perception' and data.get('self', {}).get('last_action'):
                        receipts.add(data['self']['last_action']['tick'])
            missing = sorted(set(range(100)) - actions)
            missing_receipts = sorted(set(range(100)) - receipts)
            ok = ok and not missing and not missing_receipts
            result = dict(name=case['name'], passed=ok, team=team, checks=checks, final_settlement=settlement,
                          missing_action_ticks=missing, missing_receipt_ticks=missing_receipts,
                          callback_count=len(times), published_ticks=sorted({a['tick'] for a in published if a['session_id'] == final['session_id']}),
                          callback_us_p95=times[len(times)*95//100] if times else None,
                          callback_us_max=max(times) if times else None,
                          session_id=final['session_id'], run_dir=final['run_dir'])
            report['cases'].append(result)
            (args.report_dir / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
            print(f"{'PASS' if ok else 'FAIL'} {case['name']}: value={team['resource_value']} invalid={team['invalid_actions']} missing={missing} receipt_missing={missing_receipts}", flush=True)
    finally:
        for process in (bot, referee):
            if process and process.poll() is None:
                process.send_signal(signal.SIGINT)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        botlog.close()
        reflog.close()
    return 0 if all(c['passed'] for c in report['cases']) and len(report['cases']) == len(cases) else 1

if __name__ == '__main__':
    raise SystemExit(main())
