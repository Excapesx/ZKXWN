from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import subprocess,json,hashlib,zipfile,time,sys
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'ZKXWN/versions/v10.3';OUT=ROOT/'work/round2_v103_20261028';BUILD=OUT/'build'
DEST=OUT/(sys.argv[1] if len(sys.argv)>1 else 'holdout');seedBase=int(sys.argv[2]) if len(sys.argv)>2 else 103280000;DEST.mkdir(exist_ok=False)
freeze=[]
for folder in ['src','tools','tests','ros2_ws/src/rmtc_bot9']:
 for p in sorted((SRC/folder).rglob('*')):
  if p.is_file():freeze.append({'path':p.relative_to(SRC).as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
freeze.append({'path':'CMakeLists.txt','sha256':hashlib.sha256((SRC/'CMakeLists.txt').read_bytes()).hexdigest()})
spec={'selected_level':3,'opportunity_cost':True,'safe_cost_guard':'Skip separate loss if another pickup comes before target; correction motivated by code audit after first holdout, not parameter selection.', 'selection':'B3 auditable irreversible-cost candidate, fixed phase multipliers unchanged. B2_control has a slightly higher development mean; not choosing on holdout.',
 'seed_base':seedBase,'jobs':'5 groups x16 maps x4 seats paired against each frozen baseline; 8 maps x12 same-game duel seats per baseline; stage1 40 maps; NC cost control on same 80 maps',
 'source':freeze}
(DEST/'pre_registered.json').write_text(json.dumps(spec,indent=2)+'\n',encoding='utf8')
with zipfile.ZipFile(DEST/'frozen_source.zip','w',zipfile.ZIP_DEFLATED) as z:
 for row in freeze:z.write(SRC/row['path'],row['path'])
exe=DEST/'candidate_arena.exe';exe.write_bytes((BUILD/'Release/rmtc_arena.exe').read_bytes())
jobs=[]
for baseline in ['10.1','10.2']:
 for name,field,event,size,offset in [('778','778','standard',18,0),('78g','78g','standard',18,100),('08r','08r','standard',18,200),('0gc','0gc','none',18,300),('078large','078','standard',24,400)]:
  jobs.append((baseline+'_'+name,exe,baseline,'seat',field,event,size,16,seedBase+offset,2))
 jobs.append((baseline+'_duel',exe,baseline,'duel','78g','standard',18,8,seedBase+500,2))
jobs.append(('stage1',exe,'10.1','seat','778','none',20,40,seedBase+600,1))
def run(j):
 name,binary,base,mode,field,event,size,maps,seed,stage=j
 cmd=[str(binary),'--a','10.3','--b',base,'--mode',mode,'--stage',str(stage),'--field',field,'--events',event,'--maps',str(maps),'--seed',str(seed),'--width',str(size),'--height',str(size),'--vision','4','--threads','2','--output',str(DEST/(name+'.jsonl'))]
 t=time.monotonic();p=subprocess.run(cmd,capture_output=True,timeout=1800)
 (DEST/(name+'_summary.json')).write_bytes(p.stdout);(DEST/(name+'.stderr')).write_bytes(p.stderr)
 if p.returncode:raise RuntimeError(p.stderr.decode(errors='replace'))
 d=json.loads(p.stdout);print(name,round(d['a']['score'],3),round(d['b']['score'],3),d['ab_invalid_total'],flush=True)
 return {'name':name,'command':cmd,'summary':d,'seconds':time.monotonic()-t}
t=time.monotonic()
with ThreadPoolExecutor(max_workers=3) as pool:rows=list(pool.map(run,jobs))
(DEST/'batch.json').write_text(json.dumps({'seconds':time.monotonic()-t,'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'jobs':rows},indent=2)+'\n',encoding='utf8')
print('FINAL MATCH BATCH COMPLETE',flush=True)
