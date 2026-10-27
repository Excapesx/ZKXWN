from pathlib import Path
import subprocess,json,time,concurrent.futures
ROOT=Path(__file__).resolve().parents[2]
OUT=Path(__file__).resolve().parent
variants=json.loads((OUT/'variants.json').read_text())
variants['current']={'binary':str(ROOT/'versions/v9.2/build_core/rmtc_arena')}
jobs=[]
for v in ['current','parent','route_compete']:
    for scenario,field,events in [('peers','111','standard'),('mixed','grc','standard'),('none','111','none')]:
        jobs.append((v,scenario,2,24,field,events))
for v in ['current','view_large','view_cost']:
    for size in [20,24]:
        if v=='view_large' and size==20: continue
        jobs.append((v,'stage1_'+str(size),1,size,'111','none'))
def run(job):
    v,scenario,stage,size,field,events=job
    name=v+'_'+scenario
    cmd=[variants[v]['binary'],'--stage',str(stage),'--maps','32','--seed','20261110','--a','9','--b','9.1','--field',field,'--events',events,'--width',str(size),'--height',str(size),'--threads','2','--output',str(OUT/(name+'.jsonl'))]
    start=time.time()
    p=subprocess.run(cmd,capture_output=True,text=True)
    (OUT/(name+'.stdout.json')).write_text(p.stdout)
    (OUT/(name+'.stderr.log')).write_text(p.stderr)
    (OUT/(name+'.command.json')).write_text(json.dumps({'command':cmd,'seconds':time.time()-start,'exit':p.returncode},indent=2))
    print(name,p.returncode,round(time.time()-start,1),flush=True)
    if p.returncode: raise RuntimeError(p.stderr)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    list(pool.map(run,jobs))
