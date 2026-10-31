import importlib.util
import json
from pathlib import Path
import sqlite3
from contextlib import closing
import struct
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('bag_audit',Path(__file__).parents[1]/'scripts/audit_bag.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class AuditTest(unittest.TestCase):
    def test_mismatch_and_separate_sessions(self):
        with tempfile.TemporaryDirectory() as temp:
            db=Path(temp)/'test.db3'
            with closing(sqlite3.connect(db)) as cx:
                cx.executescript('create table topics(id integer,name text);create table messages(id integer,topic_id integer,timestamp integer,data blob);')
                cx.executemany('insert into topics values(?,?)',[(1,'/team_A/action'),(2,'/team_A/perception')])
                rows=[(1,0,{'session_id':'old','tick':25,'action':'move'}),
                      (2,1,{'session_id':'old','tick':25,'self':{}}),
                      (2,2,{'session_id':'old','tick':26,'self':{'last_action':{'tick':25,'action':'wait'}}}),
                      (2,3,{'session_id':'new','tick':0,'self':{}})]
                for i,(topic,stamp,data) in enumerate(rows):
                    text=json.dumps(data).encode()+b'\0'
                    raw=b'\0\1\0\0'+struct.pack('<I',len(text))+text
                    cx.execute('insert into messages values(?,?,?,?)',(i,topic,stamp,raw))
                cx.commit()
            result=module.audit(db)
            self.assertEqual(result['sqlite_integrity'],['ok'])
            self.assertEqual(len(result['sessions']),2)
            old=next(s for s in result['sessions'] if s['session_id']=='old')
            self.assertEqual(old['receipt_mismatches'][0]['tick'],25)
            self.assertEqual(old['missing_action_ticks'],[])
            self.assertEqual(result['real_llm_qualified'],'not checked')

if __name__=='__main__':unittest.main()
