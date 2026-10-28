from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import json,hashlib,subprocess,zipfile,time
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'ZKXWN/versions/v10.4';OUT=ROOT/'work/round3_v104_20261028';DEST=OUT/'holdout';DEST.mkdir(exist_ok=False)
source=[]
for folder in ['src','tools','tests','ros2_ws/src/rmtc_bot9']:
 for p in sorted((SRC/folder).rglob('*')):
  if p.is_file():source.append({'path':p.relative_to(SRC).as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
source.append({'path':'CMakeLists.txt','sha256':hashlib.sha256((SRC/'CMakeLists.txt').read_bytes()).hexdigest()})
spec={'selected':'C0: keep all original numeric multipliers; development does not justify weakening/removal. No structural clock gates removed.',
 'variant_weights':{'C0':[1,0],'C1':[.5,0],'C2':[0,0],'C_R':[0,1],'C_P':[0,2],'C_M':[0,4]},
 'frozen_first_round':2,'frozen_second_round':3,'opportunity_cost':True,'production_alpha_percent':100,'production_restore_mask':0,
 'validation':'80 maps x4 seats per variant, references v10.3 and v10.1, four same-game duel comparisons each8 maps x12 seat assignments, stage1 20 maps C0/C2. References and repeated seats are correlated, not extra independent maps.',
 'seed_base':104280000,'source':source}
(DEST/'pre_registered.json').write_text(json.dumps(spec,indent=2)+'\n',encoding='utf8')
with zipfile.ZipFile(DEST/'frozen_source.zip','w',zipfile.ZIP_DEFLATED) as z:
 for row in source:z.write(SRC/row['path'],row['path'])
exe=DEST/'candidate_arena.exe';exe.write_bytes((OUT/'build/Release/rmtc_arena.exe').read_bytes())
jobs=[]
for variant in ['C0','C1','C2','C_R']:
 for name,field,event,size,offset in [('778','778','standard',18,0),('78g','78g','standard',18,100),('08r','08r','standard',18,200),('0gc','0gc','none',18,300),('078large','078','standard',24,400)]:
  jobs.append((variant+'_'+name,variant,'10.3','seat',field,event,size,16,104280000+offset,2))
for name,field,event,size,offset in [('778','778','standard',18,0),('78g','78g','standard',18,100),('08r','08r','standard',18,200),('0gc','0gc','none',18,300),('078large','078','standard',24,400)]:
 jobs.append(('reference101_'+name,'10.4','10.1','seat',field,event,size,16,104280000+offset,2))
for variant,base in [('10.4','10.1'),('C1','10.3'),('C2','10.3'),('C_R','10.3')]:jobs.append((variant+'_duel',variant,base,'duel','78g','standard',18,8,104280500,2))
for variant in ['C0','C2']:jobs.append((variant+'_stage1',variant,'10.3','seat','778','none',20,20,104280600,1))
def run(j):
 name,variant,base,mode,field,event,size,maps,seed,stage=j
 cmd=[str(exe),'--a',variant,'--b',base,'--mode',mode,'--stage',str(stage),'--field',field,'--events',event,'--maps',str(maps),'--seed',str(seed),'--width',str(size),'--height',str(size),'--vision','4','--threads','2','--output',str(DEST/(name+'.jsonl'))]
 t=time.monotonic();p=subprocess.run(cmd,capture_output=True,timeout=1800);(DEST/(name+'_summary.json')).write_bytes(p.stdout);(DEST/(name+'.stderr')).write_bytes(p.stderr)
 if p.returncode:raise RuntimeError(p.stderr.decode(errors='replace'))
 d=json.loads(p.stdout);print(name,round(d['a']['score'],3),round(d['b']['score'],3),d['ab_invalid_total'],flush=True)
 return {'name':name,'variant':variant,'baseline':base,'command':cmd,'summary':d,'seconds':time.monotonic()-t}
t=time.monotonic()
with ThreadPoolExecutor(max_workers=3) as pool:rows=list(pool.map(run,jobs))
(DEST/'batch.json').write_text(json.dumps({'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'seconds':time.monotonic()-t,'jobs':rows},indent=2)+'\n',encoding='utf8')
print('FROZEN HOLDOUT COMPLETE',flush=True)
