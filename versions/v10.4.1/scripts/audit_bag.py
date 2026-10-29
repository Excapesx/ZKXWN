#!/usr/bin/env python3
"""Read completed rosbag2 SQLite files; never publishes, replays, or controls a judge."""
import argparse
import collections
from contextlib import closing
import hashlib
import json
from pathlib import Path
import sqlite3
import struct


def audit(bag, seat='A'):
    bag = Path(bag).resolve()
    databases = sorted(bag.glob('*.db3')) if bag.is_dir() else [bag]
    if not databases or any(not p.is_file() for p in databases):
        raise ValueError('no SQLite bag files found')
    prefix = '/team_' + seat + '/'
    messages = collections.defaultdict(list)
    counts = collections.Counter()
    digests = {}
    integrity = []
    for db in databases:
        digests[db.name] = hashlib.sha256(db.read_bytes()).hexdigest()
        with closing(sqlite3.connect(db.as_uri() + '?mode=ro', uri=True)) as cx:
            integrity.append(cx.execute('pragma quick_check').fetchone()[0])
            topics = {tid: name for tid, name in cx.execute('select id,name from topics')}
            for tid, stamp, raw in cx.execute('select topic_id,timestamp,data from messages order by timestamp,id'):
                topic = topics[tid]
                if topic not in (prefix+'action', prefix+'perception'):
                    continue
                if len(raw)<9 or raw[:2] not in (b'\x00\x01', b'\x00\x00'):
                    raise ValueError('unsupported CDR; expected std_msgs/String')
                size = struct.unpack_from('<I' if raw[1] == 1 else '>I', raw, 4)[0]
                if size<1 or len(raw)<8+size or raw[7+size]!=0:
                    raise ValueError('truncated CDR string')
                data = json.loads(raw[8:7+size].decode('utf-8'))
                if not isinstance(data, dict) or not isinstance(data.get('tick'), int):
                    raise ValueError('invalid JSON tick')
                sid=data.get('session_id')
                if not isinstance(sid,str) or not sid:
                    raise ValueError('missing session_id')
                messages[sid].append((topic, stamp, data))
                counts[topic]+=1
    sessions=[]
    for sid, records in sorted(messages.items()):
        frames=collections.defaultdict(list); actions=collections.defaultdict(list)
        for topic, stamp, data in records:
            (frames if topic.endswith('/perception') else actions)[data['tick']].append((stamp,data))
        mismatches=[]; absent=[]; errors=[]; conflicts=[]
        for tick, rows in sorted(actions.items()):
            shapes={json.dumps(d,sort_keys=True) for _,d in rows}
            if len(shapes)>1: conflicts.append(tick)
            if tick+1 not in frames:
                absent.append(tick); continue
            receipt=frames[tick+1][-1][1].get('self',{}).get('last_action') or {}
            action=rows[-1][1]
            if receipt.get('tick')!=tick or receipt.get('action')!=action.get('action'):
                mismatches.append({'tick':tick,'sent':action.get('action'),
                    'receipt_tick':receipt.get('tick'),'receipt_action':receipt.get('action')})
            if receipt.get('error'):errors.append({'tick':tick,'error':receipt['error']})
        keys=sorted(frames)
        gaps=[{'after_tick':a,'next_tick':b,'seconds':round((frames[b][0][0]-frames[a][0][0])/1e9,6)}
              for a,b in zip(keys,keys[1:]) if b!=a+1 or frames[b][0][0]-frames[a][0][0]>2_000_000_000]
        expected=range(keys[0],keys[-1]) if keys else []
        sessions.append({'session_id':sid,'first_tick':keys[0] if keys else None,
            'last_tick':keys[-1] if keys else None,'perception_ticks':len(frames),'action_ticks':len(actions),
            'missing_action_ticks':[t for t in expected if t not in actions],
            'conflicting_actions':conflicts,'duplicate_action_messages':sum(len(v)-1 for v in actions.values()),
            'receipt_mismatches':mismatches,'receipt_unobserved_ticks':absent,'receipt_errors':errors,
            'observation_gaps_over_2s_or_missing_ticks':gaps})
    return {'scope':'offline recorded evidence; no judge controls; receive timestamps do not prove settlement deadlines',
            'sqlite_integrity':integrity,'database_sha256':digests,'counts':dict(counts),'sessions':sessions,
            'real_llm_qualified':'not checked','official_v1_2_compatible':'not checked'}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bag',type=Path,required=True)
    p.add_argument('--seat',choices=list('ABCD'),default='A')
    p.add_argument('--output',type=Path)
    args=p.parse_args()
    result=audit(args.bag,args.seat)
    text=json.dumps(result,ensure_ascii=False,indent=2)+'\n'
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(text,encoding='utf-8')
    print(text,end='')


if __name__=='__main__':
    main()
