from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import json,subprocess,zipfile,hashlib,time,sys
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'ZKXWN/versions/v10.4';OUT=ROOT/'work/round3_v104_20261028'
DEST=OUT/(sys.argv[1] if len(sys.argv)>1 else 'development');DEST.mkdir(exist_ok=False)
variants=sys.argv[2:] or ['C0','C1','C2']
exe=DEST/'rmtc_arena.exe';exe.write_bytes((OUT/'build/Release/rmtc_arena.exe').read_bytes())
with zipfile.ZipFile(DEST/'source_snapshot.zip','w',zipfile.ZIP_DEFLATED) as z:
 for folder in ['src','tools','tests','ros2_ws/src/rmtc_bot9']:
  for p in (SRC/folder).rglob('*'):
   if p.is_file():z.write(p,p.relative_to(SRC).as_posix())
 z.write(SRC/'CMakeLists.txt','CMakeLists.txt')
scenes=[('778','778','standard',18,530000),('78g','78g','standard',18,530108),('08r','08r','standard',18,530200),('0gc','0gc','none',18,530300),('078large','078','standard',24,530406)]
jobs=[(v,name,field,event,size,seed,'10.3') for v in variants for name,field,event,size,seed in scenes]
if 'C0' in variants:jobs += [('reference101',name,field,event,size,seed,'10.1') for name,field,event,size,seed in scenes]
def run(j):
 v,name,field,event,size,seed,base=j;label=v+'_'+name
 cmd=[str(exe),'--a','C0' if v=='reference101' else v,'--b',base,'--mode','seat','--stage','2','--field',field,'--events',event,'--maps','6','--seed',str(seed),'--width',str(size),'--height',str(size),'--vision','4','--threads','2','--output',str(DEST/(label+'.jsonl'))]
 p=subprocess.run(cmd,capture_output=True,timeout=1800);(DEST/(label+'_summary.json')).write_bytes(p.stdout);(DEST/(label+'.stderr')).write_bytes(p.stderr)
 if p.returncode:raise RuntimeError(p.stderr.decode(errors='replace'))
 d=json.loads(p.stdout);print(label,round(d['a']['score'],3),round(d['b']['score'],3),d['ab_invalid_total'],flush=True)
 return {'variant':v,'scene':name,'baseline':base,'command':cmd,'summary':d}
t=time.monotonic()
with ThreadPoolExecutor(max_workers=3) as pool:rows=list(pool.map(run,jobs))
(DEST/'batch.json').write_text(json.dumps({'seconds':time.monotonic()-t,'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'variants':variants,'jobs':rows},indent=2)+'\n',encoding='utf8')
for v in variants:
 rs=[x for x in rows if x['variant']==v];print(v,'MEAN',sum(x['summary']['a']['score'] for x in rs)/len(rs),flush=True)
