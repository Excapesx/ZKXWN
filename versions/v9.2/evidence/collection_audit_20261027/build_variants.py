from pathlib import Path
import hashlib,json,shlex,shutil,subprocess
ROOT=Path('/home/excapesx/ZKXWN')
SRC=ROOT/'versions/v9.2'
BUILD=SRC/'build_core'
OUT=ROOT/'build/collection-audit'
current=(SRC/'src/rmtc9/bot.cpp').read_text()
parent=subprocess.check_output(['git','show','d717fc5:versions/v9.2/src/rmtc9/bot.cpp'],cwd=ROOT,text=True)
variants={
 'parent':parent,
 'view_large':current.replace('mem_.width() * mem_.height() > 450','mem_.width() * mem_.height() > 1024'),
 'route_compete':current.replace('if (bestAttack <= 0 && p.hp >= 50 && !eventGoal)', 'if (p.hp >= 50)'),
}
variants['view_cost']=variants['view_large'].replace('const double score = cells - 0.02 * d;', 'const double score = cells / (d + 2.0);',1)
link=shlex.split((BUILD/'CMakeFiles/rmtc_arena.dir/link.txt').read_text())
manifest={}
for name,source in variants.items():
 d=OUT/name;d.mkdir(exist_ok=True)
 (d/'bot.cpp').write_text(source)
 subprocess.run(['c++','-std=c++17','-O3','-DNDEBUG','-I'+str(SRC/'src'),'-I'+str(SRC/'third_party/nlohmann/include'),'-c',str(d/'bot.cpp'),'-o',str(d/'bot.cpp.o')],check=True)
 shutil.copy2(BUILD/'librmtc9_core.a',d/'librmtc9_core.a')
 subprocess.run(['ar','r',str(d/'librmtc9_core.a'),str(d/'bot.cpp.o')],check=True)
 cmd=[]
 for i,arg in enumerate(link):
  if i and link[i-1]=='-o':arg=str(d/'rmtc_arena')
  elif arg=='librmtc9_core.a':arg=str(d/arg)
  elif arg.endswith(('.o','.a')):arg=str(BUILD/arg)
  cmd.append(arg)
 subprocess.run(cmd,check=True)
 manifest[name]={'source_sha256':hashlib.sha256(source.encode()).hexdigest(),'binary':str(d/'rmtc_arena')}
 print('built',name,flush=True)
(OUT/'variants.json').write_text(json.dumps(manifest,indent=2))
