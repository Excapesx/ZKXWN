from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import subprocess,json,hashlib,time,sys
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'work/round2_v103_20261028';DEST=OUT/(sys.argv[1] if len(sys.argv)>1 else 'holdout');seedBase=int(sys.argv[2]) if len(sys.argv)>2 else 103280000
exe=DEST/'control_no_cost_arena.exe'
jobs=[('778','778','standard',18,0),('78g','78g','standard',18,100),('08r','08r','standard',18,200),('0gc','0gc','none',18,300),('078large','078','standard',24,400)]
def run(j):
 name,field,event,size,offset=j;name='no_cost_'+name
 cmd=[str(exe),'--a','10.3','--b','10.1','--mode','seat','--stage','2','--field',field,'--events',event,'--maps','16','--seed',str(seedBase+offset),'--width',str(size),'--height',str(size),'--vision','4','--threads','2','--output',str(DEST/(name+'.jsonl'))]
 p=subprocess.run(cmd,capture_output=True,timeout=1800);(DEST/(name+'_summary.json')).write_bytes(p.stdout);(DEST/(name+'.stderr')).write_bytes(p.stderr)
 if p.returncode:raise RuntimeError(p.stderr.decode(errors='replace'))
 d=json.loads(p.stdout);print(name,round(d['a']['score'],3),round(d['b']['score'],3),flush=True)
 return {'name':name,'command':cmd,'summary':d}
with ThreadPoolExecutor(max_workers=3) as pool:rows=list(pool.map(run,jobs))
(DEST/'no_cost_batch.json').write_text(json.dumps({'level':3,'cost':False,'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'jobs':rows},indent=2)+'\n',encoding='utf8')
