from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import subprocess,json,time,hashlib,sys,zipfile
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'ZKXWN/versions/v10.3';OUT=ROOT/'work/round2_v103_20261028'
BUILD=OUT/'build';EXE=BUILD/'Release/rmtc_arena.exe'
CMAKE=Path('C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
label=sys.argv[1];level=sys.argv[2];DEST=OUT/'development'/label;DEST.mkdir(parents=True,exist_ok=False)
with zipfile.ZipFile(DEST/'source_snapshot.zip','w',zipfile.ZIP_DEFLATED) as z:
 for folder in ['src','tools','tests','ros2_ws/src/rmtc_bot9']:
  for p in (SRC/folder).rglob('*'):
   if p.is_file():z.write(p,p.relative_to(SRC).as_posix())
 z.write(SRC/'CMakeLists.txt','CMakeLists.txt')
for cmd,name in [([str(CMAKE),'-S',str(SRC),'-B',str(BUILD),'-DRMTC_ROUND2_LEVEL='+level,'-DRMTC_ENABLE_OPPORTUNITY_COST='+('OFF' if label=='B3_no_cost' else 'ON')],'configure'),([str(CMAKE),'--build',str(BUILD),'--config','Release','--target','rmtc_arena','rmtc_round2_trace','--parallel','4'],'build')]:
 with (DEST/(name+'.txt')).open('wb') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
(DEST/'rmtc_arena.exe').write_bytes(EXE.read_bytes())
jobs=[('778','778','standard',18,6,530000),('78g','78g','standard',18,6,530108),('08r','08r','standard',18,6,530200),('0gc','0gc','none',18,6,530300),('078large','078','standard',24,6,530406)]
def run(j):
 name,field,event,size,maps,seed=j;cmd=[str(EXE),'--a','10.3','--b','10.1','--mode','seat','--stage','2','--field',field,'--events',event,'--maps',str(maps),'--seed',str(seed),'--width',str(size),'--height',str(size),'--vision','4','--threads','2','--output',str(DEST/(name+'.jsonl'))]
 p=subprocess.run(cmd,capture_output=True,timeout=1200);(DEST/(name+'_summary.json')).write_bytes(p.stdout);(DEST/(name+'.stderr')).write_bytes(p.stderr)
 if p.returncode:raise RuntimeError(p.stderr.decode(errors='replace'))
 d=json.loads(p.stdout);print(label,name,round(d['a']['score'],4),round(d['b']['score'],4),flush=True)
 return {'name':name,'command':cmd,'summary':d}
t=time.monotonic()
with ThreadPoolExecutor(max_workers=3) as pool:rows=list(pool.map(run,jobs))
(DEST/'batch.json').write_text(json.dumps({'label':label,'level':int(level),'binary_sha256':hashlib.sha256(EXE.read_bytes()).hexdigest(),'seconds':time.monotonic()-t,'jobs':rows},indent=2)+'\n',encoding='utf8')
print(label,'MEAN',sum(r['summary']['a']['score'] for r in rows)/len(rows),flush=True)

