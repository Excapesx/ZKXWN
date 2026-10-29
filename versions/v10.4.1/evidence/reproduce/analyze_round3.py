from pathlib import Path
import json,statistics,random,hashlib,difflib
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'work/round3_v104_20261028';SRC=ROOT/'ZKXWN/versions/v10.4'
SCENES=['778','78g','08r','0gc','078large']
def rows(folder,name):return [json.loads(x) for x in (OUT/folder/(name+'.jsonl')).read_text(encoding='utf8').splitlines()]
def stats(rs,side,stage=2):
 mean=lambda key:statistics.mean(r[side][key] for r in rs)
 return {'score':mean('score'),'resource_value':mean('resource'),
 'resource_points':statistics.mean((.2*min(200,max(0,r[side]['resource'])) if stage==1 else .25*min(120,max(0,r[side]['resource']))) for r in rs),
 'combat_points':statistics.mean(.03*min(500,max(0,r[side]['damage'])) for r in rs),'conditional_task_points':5*mean('missions'),
 **{k:mean(k) for k in ['damage','deaths','invalid','blocked_moves','target_switches','selected_resource_lost','moves','attacks','defends','waits']}}
def ci(groups):
 rng=random.Random(10428);ms=[]
 for g in groups:ms.append([statistics.mean(x for k,x in g if k==m) for m in sorted({k for k,x in g})])
 values=sorted(statistics.mean(statistics.mean(rng.choices(x,k=len(x))) for x in ms) for _ in range(10000))
 return [values[250],values[9750]]
def compare(gs,hs,sideg='a',sideh='a'):
 dif=[]
 for g,h in zip(gs,hs):
  assert all((r['map'],r['seat'])==(s['map'],s['seat']) for r,s in zip(g,h))
  dif.append([(r['map'],r[sideg]['score']-s[sideh]['score']) for r,s in zip(g,h)])
 return {'mean_difference':statistics.mean(x for g in dif for _,x in g),'map_stratified_95':ci(dif)}
def group(reason):
 if reason in ['resource_plan','resource','route_collect']:return 'resource'
 if reason in ['reach','task_plan']:return 'task'
 if reason in ['attack','hunt','intercept','sighting_search','seek']:return 'combat_or_pursuit'
 if reason in ['information','route_view'] or reason.startswith('explore'):return 'exploration'
 return reason
result={'development':{},'traces':{}}
base=[rows('development','C0_'+n) for n in SCENES];ref=[rows('development','reference101_'+n) for n in SCENES]
for v in ['C0','C1','C2','C_R','C_P','C_M']:
 folder='development' if v in ['C0','C1','C2'] else 'restoration';gs=[rows(folder,v+'_'+n) for n in SCENES]
 result['development'][v]={'stats':stats(sum(gs,[]),'a'),'vs_C0':compare(gs,base),'vs_101':compare(gs,ref,'a','b')}
 if v=='C0':assert all(r['a']==r['b'] for g in gs for r in g),'C0 does not reproduce frozen103 aggregates'
# Primary diagnostic statistics are only the five C0 rollouts, 2500 frames.
traces=[]
for folder in ['traces_state','traces_extra']:
 for p in sorted((OUT/folder).glob('*_C0.jsonl')):
  with p.open(encoding='utf8') as f:traces.extend(json.loads(x) for x in f)
for variant in ['C1','C2']:
 keys=[variant+'_'+k for k in ['rank_changed','winner_changed','target_changed','action_changed','candidate_set_changed']]
 counts={k:sum(r[k] for r in traces) for k in keys};transitions={};examples=[]
 for r in traces:
  if r[variant+'_winner_changed'] or r[variant+'_action_changed']:
   a,b=r['probes']['C0'],r['probes'][variant];transition=group(a['reason'])+' -> '+group(b['reason']);transitions[transition]=transitions.get(transition,0)+1
   examples.append({'seed':r['seed'],'seat':r['seat'],'tick':r['tick'],'C0_reason':a['reason'],'variant_reason':b['reason'],'C0_target':a['selected_target'],'variant_target':b['selected_target'],'C0_action':a['action'],'variant_action':b['action']})
 result['traces'][variant]={'scope':'five selected development maps on C0 actual state, not an unbiased population','frames':len(traces),'counts':counts,'reason_transitions':transitions,'examples':examples}
case=[]
for folder in ['traces_state','traces_extra']:
 for p in sorted((OUT/folder).glob('*.jsonl')):
  rs=[json.loads(x) for x in p.read_text().splitlines()];collected=[]
  for r in rs:
   if r['after']['resource']>r['resource']:
    for x in r['truth_removed_resources']:
     if x['pos']==r['after']['pos']:collected.append({'id':x['id'],'value':x['value'],'tick':r['tick']})
  case.append({'name':p.stem,'final':rs[-1]['after'],'collected':collected})
result['case_rollouts']=case
if (OUT/'holdout/batch.json').exists():
 final={};gs0=[rows('holdout','C0_'+n) for n in SCENES];refs=[rows('holdout','reference101_'+n) for n in SCENES]
 for v in ['C0','C1','C2','C_R']:
  gs=[rows('holdout',v+'_'+n) for n in SCENES]
  assert all(r['b']==s['b'] for g,h in zip(gs,gs0) for r,s in zip(g,h)), 'frozen103 reference differs between variants'
  if v=='C0':assert all(r['a']==r['b'] for g in gs for r in g)
  final[v]={'stats':stats(sum(gs,[]),'a'),'vs_103':compare(gs,gs0,'a','b'),'vs_101':compare(gs,refs,'a','b')}
 assert all(r['a']==s['a'] for g,h in zip(gs0,refs) for r,s in zip(g,h)), 'C0 and sealed production defaults differ'
 final['reference101']=stats(sum(refs,[]),'b')
 duel={}
 for v in ['10.4','C1','C2','C_R']:
  rs=rows('holdout',v+'_duel');duel[v]={'a':stats(rs,'a'),'b':stats(rs,'b'),'ci':ci([[(r['map'],r['difference']) for r in rs]]),'higher':sum(r['difference']>1e-8 for r in rs),'equal':sum(abs(r['difference'])<=1e-8 for r in rs),'lower':sum(r['difference']<-1e-8 for r in rs)}
 final['duel']=duel
 final['stage1']={v:{'stats':stats(rows('holdout',v+'_stage1'),'a',1),'all_equal':all(r['a']['score']==r['b']['score'] and r['a']['resource']==r['b']['resource'] for r in rows('holdout',v+'_stage1'))} for v in ['C0','C2']}
 spec=json.loads((OUT/'holdout/pre_registered.json').read_text());final['source_unchanged']=all(hashlib.sha256((SRC/r['path']).read_bytes()).hexdigest()==r['sha256'] for r in spec['source'])
 result['holdout']=final
(OUT/'analysis.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
print(json.dumps({k:{v:x['stats']['score'] for v,x in d.items()} for k,d in result.items() if k=='development'},ensure_ascii=False))
if 'holdout' in result:print(json.dumps({v:{'score':result['holdout'][v]['stats']['score'],'vs103':result['holdout'][v]['vs_103'],'vs101':result['holdout'][v]['vs_101']} for v in ['C0','C1','C2','C_R']},ensure_ascii=False))
print('Trace summary:',json.dumps({v:x['counts'] for v,x in result['traces'].items()},ensure_ascii=False))
