from pathlib import Path
import subprocess,json,hashlib
ROOT=Path(__file__).resolve().parents[1]; REPO=ROOT/'ZKXWN'
OLD=REPO/'versions/v10.2'; NEW=REPO/'versions/v10.3'; OUT=ROOT/'work/round2_v103_20261028'
if subprocess.check_output(['git','-C',str(REPO),'status','--porcelain']): raise SystemExit('Dirty repository; inspect first')
if NEW.exists(): raise SystemExit('Candidate exists; refuse overwrite')
OUT.mkdir(exist_ok=False)
for name in filter(None,subprocess.check_output(['git','-C',str(REPO),'ls-files','-z','versions/v10.2']).decode().split('\0')):
    src=REPO/name; dst=NEW/src.relative_to(OLD); dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(src.read_bytes())
history=NEW/'历史10.2说明';history.mkdir()
for name in ['README.md','更新日志.md','修复与验证报告.md','来源及验证.json','变更文件.json','文件清单.json','evidence']:
    src=NEW/name
    if src.exists():src.rename(history/name)
rows=[]
for src in sorted((OLD/'src/rmtc9').glob('*')):
    if not src.is_file():continue
    raw=src.read_bytes();data=raw.replace(b'rmtc9',b'rmtc102')
    dst=NEW/'baseline_v102/src/rmtc102'/src.name;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(data)
    rows.append({'path':src.relative_to(OLD).as_posix(),'sha256':hashlib.sha256(raw).hexdigest(),'transformed_sha256':hashlib.sha256(data).hexdigest()})
(NEW/'baseline_v102/source_manifest.json').write_text(json.dumps({'commit':'235ee2421b72feed587cd1d8773c6c46a2876bca','transformation':'rmtc9 namespace/include only -> rmtc102','files':rows},indent=2)+'\n',encoding='utf8')
for name in ['VERSION','CMakeLists.txt','ros2_ws/src/rmtc_bot9/CMakeLists.txt','ros2_ws/src/rmtc_bot9/package.xml','ros2_ws/src/rmtc_bot9/src/node.cpp']:
    p=NEW/name;p.write_text(p.read_text(encoding='utf8').replace('10.2.0','10.3.0').replace('HERO-10.2','HERO-10.3'),encoding='utf8',newline='\n')
p=NEW/'tools/bench/v101_adapter.cpp'
(NEW/'tools/bench/v102_adapter.cpp').write_text(p.read_text(encoding='utf8').replace('rmtc101','rmtc102').replace('v101','v102').replace('V101','V102').replace('10.1','10.2'),encoding='utf8',newline='\n')
p=NEW/'tools/bench/opponent_adapter.hpp';p.write_text(p.read_text(encoding='utf8')+'\nstd::unique_ptr<IPolicy> make_v102_bot(int stage,int ticks);\n',encoding='utf8',newline='\n')
p=NEW/'CMakeLists.txt';s=p.read_text(encoding='utf8');s+='''
set(RMTC_ROUND2_LEVEL "0" CACHE STRING "0=B0 frozen behavior; 1=B1 race; 2=B2 route; 3=B3 opportunity cost")
if(NOT RMTC_ROUND2_LEVEL MATCHES "^[0123]$")
 message(FATAL_ERROR "RMTC_ROUND2_LEVEL must be 0..3")
endif()
target_compile_definitions(rmtc9_core PUBLIC RMTC_ROUND2_LEVEL=${RMTC_ROUND2_LEVEL})
add_library(baseline102 STATIC baseline_v102/src/rmtc102/types.cpp baseline_v102/src/rmtc102/grid.cpp baseline_v102/src/rmtc102/parse.cpp baseline_v102/src/rmtc102/bot.cpp baseline_v102/src/rmtc102/events.cpp baseline_v102/src/rmtc102/battle.cpp)
target_include_directories(baseline102 PUBLIC baseline_v102/src third_party/nlohmann/include)
if(MSVC)
 target_compile_options(baseline102 PUBLIC /utf-8)
endif()
target_sources(rmtc_arena PRIVATE tools/bench/v102_adapter.cpp)
target_link_libraries(rmtc_arena baseline102)
''';p.write_text(s,encoding='utf8',newline='\n')
p=NEW/'tools/bench/bench_main.cpp';s=p.read_text(encoding='utf8').replace("if (version == '2')", "if (version == '4') return make_v102_bot(stage, ticks);\n    if (version == '2')").replace('if (v=="10.2") return \'9\';','if (v=="10.3") return \'9\';\n    if (v=="10.2") return \'4\';').replace('9023187grcw','90423187grcw');p.write_text(s,encoding='utf8',newline='\n')
print(NEW)
