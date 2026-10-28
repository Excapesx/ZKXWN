from pathlib import Path
import json,statistics,random,hashlib,difflib,zipfile,sys
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'work/round2_v103_20261028';P=OUT/(sys.argv[1] if len(sys.argv)>1 else 'holdout')
names=['778','78g','08r','0gc','078large']
def read(name):return [json.loads(x) for x in (P/(name+'.jsonl')).read_text(encoding='utf-8-sig').splitlines()]
def stats(rows,side,stage=2):
 def mean(key):return statistics.mean(r[side][key] for r in rows)
 return {'score':mean('score'),'resource':mean('resource'),'resource_points':statistics.mean((.2*min(max(r[side]['resource'],0),200) if stage==1 else .25*min(max(r[side]['resource'],0),120)) for r in rows),
 'combat_points':statistics.mean(.03*min(max(r[side]['damage'],0),500) for r in rows),'task_points':5*mean('missions'),
 **{k:mean(k) for k in ['damage','deaths','invalid','blocked_moves','target_switches','selected_resource_lost']}}
def interval(groups):
 rng=random.Random(10328);maps=[]
 for g in groups:
  maps.append([statistics.mean(x for key,x in g if key==m) for m in sorted({key for key,x in g})])
 xs=sorted(statistics.mean(statistics.mean(rng.choices(g,k=len(g))) for g in maps) for _ in range(10000))
 return [xs[250],xs[9750]]
result={}
for base in ['10.1','10.2']:
 groups=[read(base+'_'+n) for n in names];rows=sum(groups,[])
 result[base]={'candidate':stats(rows,'a'),'baseline':stats(rows,'b'),'ci':interval([[(r['map'],r['difference']) for r in g] for g in groups]),'maps':80,'paired_runs_per_version':len(rows)}
 duel=read(base+'_duel');result[base]['duel']={'candidate':stats(duel,'a'),'baseline':stats(duel,'b'),'ci':interval([[(r['map'],r['difference']) for r in duel]]),'maps':8,'games':len(duel),'higher':sum(r['difference']>1e-8 for r in duel),'equal':sum(abs(r['difference'])<=1e-8 for r in duel),'lower':sum(r['difference']<-1e-8 for r in duel)}
a=[read('10.1_'+n) for n in names];b=[read('10.2_'+n) for n in names];nc=[read('no_cost_'+n) for n in names]
assert all(x['a']==y['a'] for g,h in zip(a,b) for x,y in zip(g,h)), 'A re-run disagreement'
diff=[]
for g,h in zip(a,nc):
 assert all((r['map'],r['seat'])==(s['map'],s['seat']) and r['b']==s['b'] for r,s in zip(g,h)), 'Cost comparison baseline/map mismatch'
 diff.append([(r['map'],r['a']['score']-s['a']['score']) for r,s in zip(g,h)])
result['cost_control']={'on':stats(sum(a,[]),'a'),'off':stats(sum(nc,[]),'a'),'ci':interval(diff)}
st=read('stage1');result['stage1']={'candidate':stats(st,'a',1),'baseline':stats(st,'b',1),'score_and_resource_equal_all_maps':all(r['a']['score']==r['b']['score'] and r['a']['resource']==r['b']['resource'] for r in st)}
result['frozen_files_unchanged']=all(hashlib.sha256((ROOT/'ZKXWN/versions/v10.3'/x['path']).read_bytes()).hexdigest()==x['sha256'] for x in json.loads((P/'pre_registered.json').read_text())['source'])
(OUT/('analysis_v2.json' if len(sys.argv)>1 else 'analysis.json')).write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
dev=OUT/'development';previous=None
for f in [dev/name/'source_snapshot.zip' for name in ['B0','B1','B2','B2_control','B3_no_cost','B3']]:
 with zipfile.ZipFile(f) as z:files={n:z.read(n).decode('utf-8-sig',errors='replace') for n in z.namelist()}
 if previous:
  label,old=previous;delta=[]
  for n in sorted(set(old)|set(files)):
   if old.get(n)!=files.get(n):delta.extend(difflib.unified_diff(old.get(n,'').splitlines(True),files.get(n,'').splitlines(True),fromfile=label+'/'+n,tofile=f.parent.name+'/'+n))
  (f.parent/'source_changes_from_previous.diff').write_text(''.join(delta),encoding='utf8')
 previous=(f.parent.name,files)
print(json.dumps(result,indent=2))

