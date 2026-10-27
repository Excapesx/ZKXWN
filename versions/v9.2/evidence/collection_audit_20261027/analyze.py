from pathlib import Path
import collections,json,random,statistics
OUT=Path(__file__).resolve().parent
def read(name):
    return [json.loads(s) for s in (OUT/(name+'.jsonl')).read_text().splitlines()]
def compare(new,old):
    a={(x['map'],x.get('seat',0)):x['a'] for x in read(new)}
    b={(x['map'],x.get('seat',0)):x['a'] for x in read(old)}
    assert a.keys()==b.keys()
    ds=collections.defaultdict(list)
    for k in a:ds[k[0]].append(a[k]['score']-b[k]['score'])
    means=[statistics.mean(v) for v in ds.values()]
    rng=random.Random(2710)
    draws=sorted(statistics.mean(rng.choices(means,k=len(means))) for _ in range(10000))
    return {'new':new,'old':old,'maps':len(means),'games_each':len(a),
        'delta':{metric:statistics.mean(a[k][metric]-b[k][metric] for k in a) for metric in ['score','resource','missions','mission_high','deaths']},
        'paired_map_bootstrap_95_score':[draws[249],draws[9749]],
        'wins':sum(a[k]['score']>b[k]['score'] for k in a),
        'ties':sum(a[k]['score']==b[k]['score'] for k in a),
        'losses':sum(a[k]['score']<b[k]['score'] for k in a)}
out={'comparisons':[],'diagnostics':{}}
for scenario in ['peers','mixed','none']:
    out['comparisons'].append(compare('current_'+scenario,'parent_'+scenario))
    out['comparisons'].append(compare('route_compete_'+scenario,'current_'+scenario))
for scenario in ['peers','none']:
    if (OUT/('holdout_parent_'+scenario+'.stdout.json')).exists():
        out['comparisons'].append(compare('holdout_current_'+scenario,'holdout_parent_'+scenario))
for size in [20,24]:
    out['comparisons'].append(compare('view_cost_stage1_'+str(size),'current_stage1_'+str(size)))
out['comparisons'].append(compare('view_large_stage1_24','current_stage1_24'))
for stem in ['diagnostic_stage1_20','diagnostic_stage2']:
    rows=read(stem);cats=collections.Counter();high=collections.Counter();miss=collections.Counter();reasons=collections.Counter()
    for x in rows:
        for r in x['resources']:
            cat='own' if r['collector']==x['seat'] else ('unseen_other' if r['collector']>=0 and r['first_seen']<0 else 'seen_other' if r['collector']>=0 else 'unseen_left' if r['first_seen']<0 else 'seen_left')
            cats[cat]+=1
            if r['value']==20:high[cat]+=1
        reasons.update(x['reasons_before_depletion'])
        if x['high_count']==0:
            seen=[r for r in x['resources'] if r['value']==20 and r['first_seen']>=0]
            if not seen:miss['no_high_seen']+=1
            elif any(r['taken_tick']-r['first_seen']>=max(0,r['first_distance_lower_bound']-1) for r in seen):miss['seen_with_possible_time']+=1
            else:miss['seen_but_too_late']+=1
    depletion=[x['depletion_tick'] for x in rows if x['depletion_tick']>=0]
    out['diagnostics'][stem]={'games':len(rows),'resources':dict(cats),'high_resources':dict(high),'high_task_failed':dict(miss),'depletion_mean':statistics.mean(depletion),'depletion_median':statistics.median(depletion),'depletion_min':min(depletion),'depletion_max':max(depletion),'reasons':dict(reasons)}
for scenario in ['peers','mixed']:
    if (OUT/('path_view_'+scenario+'.stdout.json')).exists() and (OUT/('path_view_'+scenario+'.stdout.json')).stat().st_size:
        out['comparisons'].append(compare('path_view_'+scenario,'current_'+scenario))
for prefix in ['', 'holdout_']:
    if (OUT/(prefix+'path_view_none.stdout.json')).exists() and (OUT/(prefix+'path_view_none.stdout.json')).stat().st_size:
        out['comparisons'].append(compare(prefix+'path_view_none',prefix+'current_none'))
out['summaries']={p.name.replace('.stdout.json',''):json.loads(p.read_text())['a'] for p in OUT.glob('*.stdout.json') if p.stat().st_size}
(OUT/'analysis.json').write_text(json.dumps(out,indent=2,ensure_ascii=False))
print(json.dumps({k:v for k,v in out.items() if k!='summaries'},indent=2,ensure_ascii=False))
