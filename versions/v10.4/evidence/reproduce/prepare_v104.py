from pathlib import Path
import json,hashlib,shutil,subprocess
ROOT=Path(__file__).resolve().parents[1];REPO=ROOT/'ZKXWN';OLD=REPO/'versions/v10.3';NEW=REPO/'versions/v10.4';OUT=ROOT/'work/round3_v104_20261028'
if NEW.exists() or OUT.exists():raise SystemExit('Destination exists; inspect before continuing')
status=subprocess.check_output(['git','-C',str(REPO),'status','--porcelain'],text=True)
if status.strip()!='?? versions/v10.3/':raise SystemExit('Unexpected Git modifications: '+status)
manifest=json.loads((OLD/'文件清单.json').read_text(encoding='utf8'))
for row in manifest:
 p=OLD/row['path']
 if p.stat().st_size!=row['bytes'] or hashlib.sha256(p.read_bytes()).hexdigest()!=row['sha256']:raise SystemExit('10.3 manifest mismatch: '+row['path'])
freeze=json.loads((ROOT/'work/round2_v103_20261028/holdout_v2/pre_registered.json').read_text())
assert all(hashlib.sha256((OLD/r['path']).read_bytes()).hexdigest()==r['sha256'] for r in freeze['source'])
OUT.mkdir();shutil.copytree(OLD,NEW)
baseline=[]
for p in sorted(OLD.rglob('*')):
 if p.is_file():baseline.append({'path':p.relative_to(OLD).as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size})
(OUT/'baseline_v103_full_manifest.json').write_text(json.dumps({'source':'uncommitted filesystem v10.3, NOT Git HEAD','git_head':subprocess.check_output(['git','-C',str(REPO),'rev-parse','HEAD'],text=True).strip(),'files':baseline},ensure_ascii=False,indent=2)+'\n',encoding='utf8')
history=NEW/'历史10.3说明';history.mkdir()
for name in ['README.md','更新日志.md','修复与验证报告.md','来源及验证.json','变更文件.json','文件清单.json','evidence']:
 p=NEW/name
 if p.exists():p.rename(history/name)
rows=[]
for p in sorted((OLD/'src/rmtc9').glob('*')):
 if not p.is_file():continue
 raw=p.read_bytes();data=raw.replace(b'rmtc9',b'rmtc103');dst=NEW/'baseline_v103/src/rmtc103'/p.name;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(data)
 rows.append({'path':p.relative_to(OLD).as_posix(),'original_sha256':hashlib.sha256(raw).hexdigest(),'transformed_sha256':hashlib.sha256(data).hexdigest()})
(NEW/'baseline_v103/source_manifest.json').write_text(json.dumps({'source':'verified uncommitted v10.3 filesystem','transformation':'rmtc9 -> rmtc103 namespace/include only','files':rows},indent=2)+'\n',encoding='utf8')
for name in ['VERSION','CMakeLists.txt','ros2_ws/src/rmtc_bot9/CMakeLists.txt','ros2_ws/src/rmtc_bot9/package.xml','ros2_ws/src/rmtc_bot9/src/node.cpp']:
 p=NEW/name;p.write_text(p.read_text(encoding='utf-8-sig').replace('10.3.0','10.4.0').replace('HERO-10.3','HERO-10.4'),encoding='utf8',newline='\n')
source=(OLD/'tools/bench/v102_adapter.cpp').read_text(encoding='utf-8-sig').replace('rmtc102','rmtc103').replace('v102','v103').replace('V102','V103').replace('10.2','10.3')
source=source.replace('const char* name() const override { return "v10.3"; }','''const char* name() const override { return "v10.3"; }
    PolicyAudit audit() const override {
        PolicyAudit a;const auto& d=bot_.diagnostics();a.switches=d.targetSwitches;a.detailed=true;
        if(d.selectedValid && d.selected.kind=="resource_plan"){a.targetId=d.selected.id;a.target={d.selected.target.x,d.selected.target.y};}
        return a;
    }''')
(NEW/'tools/bench/v103_adapter.cpp').write_text(source,encoding='utf8',newline='\n')
p=NEW/'tools/bench/opponent_adapter.hpp';p.write_text(p.read_text(encoding='utf8')+'\nstd::unique_ptr<IPolicy> make_v103_bot(int stage,int ticks);\n',encoding='utf8',newline='\n')
print('Verified 10.3 files:',len(baseline),'copied candidate:',NEW)
