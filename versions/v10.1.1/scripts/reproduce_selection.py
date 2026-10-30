#!/usr/bin/env python3
"""Rebuild the paired selection experiment from tracked sources; never uses ROS/AI."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import subprocess

VERSION = Path(__file__).resolve().parents[1]
REPO = VERSION.parents[1]
SCENES = [
    ('peers_events',24,'222','standard',2,0,20305000),
    ('mixed_events',24,'78g','standard',2,0,20306000),
    ('collectors_none',24,'0gc','none',2,0,20307000),
    ('events_delay',18,'278','standard',8,2,20308000),
]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,default=REPO/'build/v1011-selection-reproduction')
    p.add_argument('--maps',type=int,default=32,help='Per scene; historical experiment used 32')
    p.add_argument('--jobs',type=int,default=3)
    p.add_argument('--build-jobs',type=int,default=4)
    args=p.parse_args()
    if args.maps<1 or args.maps>32 or not 1<=args.jobs<=8:p.error('maps: 1..32; jobs: 1..8')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    if (out/'design.json').exists():p.error('Choose a fresh output directory to preserve prior evidence')
    # The frozen comparator must still be the original 560 policy.
    stock=REPO/'versions/v10.2/baseline_v101/src/rmtc101/bot.cpp'
    assert 'constexpr double kHuntCap = 560.0;' in stock.read_text()
    assert 'kCombatKeepCap' not in stock.read_text()
    subprocess.run(['cmake','-S',str(VERSION/'tools/selection'),'-B',str(out/'build'),'-DCMAKE_BUILD_TYPE=Release'],check=True)
    subprocess.run(['cmake','--build',str(out/'build'),'-j',str(args.build_jobs)],check=True)
    exe=out/'build/selection_arena'
    jobs=[]
    for name,size,field,events,latency,noise,seed in SCENES:
        for part in range(0,args.maps,8):
            jobs.append(dict(name=f'{name}_{part//8}',stage=2,size=size,field=field,events=events,
                latency=latency,noise=noise,seed=seed+part,maps=min(8,args.maps-part)))
    jobs.append(dict(name='stage1_identity',stage=1,size=20,field='222',events='none',
        latency=2,noise=0,seed=20309000,maps=64 if args.maps==32 else args.maps))
    design=dict(jobs=jobs,binary_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
        labels={'candidate':'final v10.1.1 policy','10.1':'frozen upstream 10.1, 560 threshold'},
        limits='Structured oracle; no real LLM, qualification, ROS, DDS or exact formal event-draw distribution')
    (out/'design.json').write_text(json.dumps(design,indent=2))
    def run(j):
        cmd=[str(exe),'--stage',str(j['stage']),'--width',str(j['size']),'--height',str(j['size']),
             '--a','candidate','--b','10.1','--field',j['field'],'--events',j['events'],
             '--latency',str(j['latency']),'--noise',str(j['noise']),'--seed',str(j['seed']),
             '--maps',str(j['maps']),'--threads','2','--vision','4','--output',str(out/(j['name']+'.jsonl'))]
        result=subprocess.run(cmd,capture_output=True,text=True,timeout=1800)
        (out/(j['name']+'.stdout.json')).write_text(result.stdout)
        (out/(j['name']+'.stderr.txt')).write_text(result.stderr)
        (out/(j['name']+'.command.json')).write_text(json.dumps(dict(command=cmd,returncode=result.returncode),indent=2))
        assert result.returncode==0,result.stderr[-2000:]
        rows=[json.loads(s) for s in (out/(j['name']+'.jsonl')).read_text().splitlines()]
        assert len(rows)==j['maps']*(4 if j['stage']==2 else 1)
        if j['stage']==1:assert all(r['a']==r['b'] for r in rows)
        print('DONE',j['name'],flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:list(pool.map(run,jobs))

if __name__=='__main__':main()
