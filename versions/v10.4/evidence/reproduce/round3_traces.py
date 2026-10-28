from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import json,subprocess,time,sys
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'work/round3_v104_20261028';DEST=OUT/(sys.argv[1] if len(sys.argv)>1 else 'traces');DEST.mkdir(exist_ok=False)
exe=OUT/'build/Release/rmtc_phase_trace.exe'
jobs=[(seed,seat,field,size,event,v) for seed,seat,field,size,event in ([(530005,1,'778',18,'standard'),(530303,3,'0gc',18,'none')] if len(sys.argv)>2 and sys.argv[2]=='extra' else [(530112,0,'78g',18,'standard'),(530301,1,'0gc',18,'none'),(530408,2,'078',24,'standard')]) for v in ['C0','C1','C2']]
def run(j):
 seed,seat,field,size,event,v=j;name=f'{seed}_{seat}_{v}';cmd=[str(exe),str(seed),str(seat),v,field,str(size),event];t=time.monotonic()
 with (DEST/(name+'.jsonl')).open('wb') as f:
  p=subprocess.run(cmd,stdout=f,stderr=subprocess.PIPE,timeout=1200)
 (DEST/(name+'.stderr')).write_bytes(p.stderr)
 if p.returncode:raise RuntimeError(p.stderr.decode(errors='replace'))
 last=None;counts={key:0 for key in ['C1_rank_changed','C1_action_changed','C1_candidate_set_changed','C1_winner_changed','C1_target_changed','C2_rank_changed','C2_action_changed','C2_candidate_set_changed','C2_winner_changed','C2_target_changed']}
 for line in (DEST/(name+'.jsonl')).open(encoding='utf8'):
  last=json.loads(line)
  for key in counts:counts[key]+=last[key]
 result={'name':name,'actual':v,'seed':seed,'seat':seat,'command':cmd,'frames':500,'counts':counts,'final':last['after'],'seconds':time.monotonic()-t}
 print(name,result['final']['score'],counts,flush=True);return result
with ThreadPoolExecutor(max_workers=2) as pool:rows=list(pool.map(run,jobs))
(DEST/'summary.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf8')
