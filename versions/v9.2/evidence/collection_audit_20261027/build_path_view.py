from pathlib import Path
import subprocess,json,shlex,shutil,hashlib
root=Path.cwd();src=root/'versions/v9.2';build=src/'build_core';out=root/'build/collection-audit/path_view';out.mkdir(exist_ok=True)
s=(src/'src/rmtc9/bot.cpp').read_text()
old='''        const int8_t dir = field_.firstAt(chosen.pos);
        if(dir==kFirstWait)'''
new='''        int8_t dir = field_.firstAt(chosen.pos);
        // Experiment: only break shortest-path ties when no events/enemies need risk scoring.
        if (dir >= 0 && !hz_.any() && diagnostics_.visibleEnemies == 0) {
            const int distance = field_.at(chosen.pos);
            const Field reverse = bfs(mask_, chosen.pos, distance);
            const auto coverage = [&](int candidate) {
                const Point next{p.self.x + kDirX[candidate], p.self.y + kDirY[candidate]};
                int gain = 0;
                for (int y=next.y-p.vision;y<=next.y+p.vision;++y)
                    for (int x=next.x-p.vision;x<=next.x+p.vision;++x)
                        if (mem_.inside({x,y}) && mem_.at({x,y}) == kUnknown) ++gain;
                return gain;
            };
            int bestCoverage = coverage(dir);
            for (int candidate=0;candidate<4;++candidate) {
                const Point next{p.self.x+kDirX[candidate],p.self.y+kDirY[candidate]};
                if (!safe_step(p,candidate) || reverse.at(next) != distance-1) continue;
                const int gain = coverage(candidate);
                if (gain > bestCoverage) { bestCoverage=gain; dir=candidate; }
            }
        }
        if(dir==kFirstWait)'''
assert s.count(old)==1
s=s.replace(old,new);(out/'bot.cpp').write_text(s)
subprocess.run(['c++','-std=c++17','-O3','-DNDEBUG','-I'+str(src/'src'),'-I'+str(src/'third_party/nlohmann/include'),'-c',str(out/'bot.cpp'),'-o',str(out/'bot.cpp.o')],check=True)
shutil.copy2(build/'librmtc9_core.a',out/'librmtc9_core.a');subprocess.run(['ar','r',str(out/'librmtc9_core.a'),str(out/'bot.cpp.o')],check=True)
link=shlex.split((build/'CMakeFiles/rmtc_arena.dir/link.txt').read_text());cmd=[]
for i,arg in enumerate(link):
 if i and link[i-1]=='-o':arg=str(out/'rmtc_arena')
 elif arg=='librmtc9_core.a':arg=str(out/arg)
 elif arg.endswith(('.a','.o')):arg=str(build/arg)
 cmd.append(arg)
subprocess.run(cmd,check=True)
p=out.parent/'variants.json';v=json.loads(p.read_text());v['path_view']={'source_sha256':hashlib.sha256(s.encode()).hexdigest(),'binary':str(out/'rmtc_arena')};p.write_text(json.dumps(v,indent=2))
