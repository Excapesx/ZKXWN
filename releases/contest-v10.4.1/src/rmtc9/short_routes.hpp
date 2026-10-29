#pragma once
// Included after CandidateAudit and choice_confidence in opportunity_evaluator.
// Confidence is a scenario weight, not a calibrated probability.
namespace rmtc9 {
inline ScoreEstimate route_outcome(const Perception& p,int normal,int high,int value,
    int addNormal,int addHigh,const std::set<Point>& visited) {
    ScoreEstimate s;s.resource=resource_points(p.resourceValue+value)-resource_points(p.resourceValue);
    std::set<std::string> done;
    for(const auto& m:p.missions) {
        if(!done.insert(m.id).second || score_mission_done(p,m.id) || m.target<=0)continue;
        double before=0,after=0;
        if(m.type=="normal_resources"){before=normal;after=normal+addNormal;}
        else if(m.type=="high_resources"){before=high;after=high+addHigh;}
        else if(m.type=="resource_value"){before=p.resourceValue;after=p.resourceValue+value;}
        else if(m.type=="reach" && m.position){if(visited.count(*m.position)){s.taskConditional+=5;s.reachTaskConditional+=5;}continue;}
        else continue;
        if(before<m.target && after>=m.target)s.taskConditional+=5;
        else if(after>before)s.progressAux+=.25*std::max(0.0,std::min(after/m.target,1.0)-std::min(before/m.target,1.0));
    }
    s.progressAux=std::min(.5,s.progressAux);return s;
}
struct RoutePickup { Resource resource;int eta=0,rival=-1;double commitment=0; };
inline ScoreEstimate weighted_route_score(const Perception& p,int normal,int high,
    const std::vector<RoutePickup>& pickups,const std::set<Point>& visited,int delay=0) {
    // Merge equal outcomes by (raw value, normal count, high count). Cap and
    // mission completion are evaluated AFTER each possible collection outcome.
    using Key=std::tuple<int,int,int>;
    std::map<Key,double> states{{Key{0,0,0},1}};
    std::set<std::string> seenIds;std::set<Point> seenPositions;
    double denial=0;
    for(const auto& item:pickups) {
        if(!seenIds.insert(item.resource.id).second || !seenPositions.insert(item.resource.pos).second)continue;
        int age=item.resource.observedTick<0?0:std::max(0,p.tick-item.resource.observedTick);
        if(kRound2Level<3)age+=delay; // B2 control retains the old age-delay penalty.
        const double c=choice_confidence(item.eta+delay,item.rival,age,item.commitment);
        std::map<Key,double> next;
        for(const auto& state:states) {
            const auto [v,n,h]=state.first;
            next[state.first]+=state.second*(1-c);
            next[{v+item.resource.value,n+(item.resource.value<20),h+(item.resource.value>=20)}]+=state.second*c;
        }
        // Conservative fallback for malformed/custom values creating too many
        // distinct scenarios. Standard 10/20 resources stay far below 256.
        if(next.size()>256)return {};
        states=std::move(next);
        if(item.rival>=0)denial+=.025*item.resource.value*c;
    }
    ScoreEstimate out;
    for(const auto& state:states) {
        const auto [v,n,h]=state.first;const auto s=route_outcome(p,normal,high,v,n,h,visited);
        out.resource+=state.second*s.resource;out.taskConditional+=state.second*s.taskConditional;
        out.reachTaskConditional+=state.second*s.reachTaskConditional;out.progressAux+=state.second*s.progressAux;
    }
    out.denialAux=denial;return out;
}

inline OpportunityResult short_resource_routes(const Perception& p,const WorldMap& mem,
    const BlockMask& mask,const Field& field,const std::vector<Field>& enemies,int remaining,
    const HazardField* hazards) {
    OpportunityResult out;
    struct Node {Resource r;int d=0,rival=-1;double choice=0,rank=0;};
    std::vector<Node> nodes;
    // Only currently visible rival distance fields are supplied by production.
    auto contest=[&](const Resource& r,int ours) {
        int rival=-1;double choice=0,worst=1;
        for(const auto& e:enemies) {
            int q=e.at(r.pos);if(q<0)continue;
            int other=1<<20;for(const auto& item:mem.resources())if(item.first!=r.id){int z=e.at(item.second.pos);if(z>=0)other=std::min(other,z);}
            double c=other+1<q?.25:other<=q+1?.55:.85;
            double available=choice_confidence(ours,q,0,c);
            if(available<worst){rival=q;choice=c;worst=available;}
        }
        return std::make_pair(rival,choice);
    };
    for(const auto& item:mem.resources()) {
        const auto& r=item.second;int d=field.at(r.pos);if(d<=0||d>remaining)continue;
        auto [rival,choice]=contest(r,d);int age=r.observedTick<0?0:std::max(0,p.tick-r.observedTick);
        double rank=collection_score(p,mem.normalCollected(),mem.highCollected(),{r}).utility()*choice_confidence(d,rival,age,choice)/(d+1.0);
        nodes.push_back({r,d,rival,choice,rank});
    }
    std::set<Point> seen;
    for(const auto& m:p.missions)if(m.type=="reach"&&m.position&&!score_mission_done(p,m.id)&&seen.insert(*m.position).second) {
        int d=field.at(*m.position);if(d<=0||d>remaining)continue;
        if(std::any_of(nodes.begin(),nodes.end(),[&](const Node& n){return n.r.pos==*m.position;}))continue;
        nodes.push_back({Resource{"@reach:"+m.id,*m.position,0,p.tick},d,-1,0,5/(d+1.0)});
    }
    std::stable_sort(nodes.begin(),nodes.end(),[](const auto&a,const auto&b){return a.rank>b.rank;});
    if(nodes.size()>12)nodes.resize(12);
    int n=static_cast<int>(nodes.size());if(!n)return out;
    std::vector<Field> reverse;
    for(const auto& node:nodes){reverse.push_back(bfs(mask,node.r.pos,std::min(24,remaining)));++out.bfsRuns;}
    // Materialize deterministic public shortest paths so incidental pickups and
    // task coordinates are evaluated at their actual route time. Replan next frame.
    std::vector<std::vector<std::vector<Point>>> paths(n+1,std::vector<std::vector<Point>>(n));
    for(int from=0;from<=n;++from)for(int to=0;to<n;++to) {
        Point start=from==n?p.self:nodes[from].r.pos;
        int distance=from==n?nodes[to].d:reverse[to].at(start);
        if(distance<=0||distance>24)continue;
        Point at=start;auto& path=paths[from][to];
        for(int t=0;t<distance;++t) {
            int dir=-1;
            if(t==0&&from==n)dir=field.firstAt(nodes[to].r.pos);
            if(dir==kFirstWait){path.push_back(at);continue;}
            if(dir<0)for(int k=0;k<4;++k){Point next{at.x+kDirX[k],at.y+kDirY[k]};if(!mask.blocked(next)&&reverse[to].at(next)==reverse[to].at(at)-1){dir=k;break;}}
            if(dir<0||dir>3){path.clear();break;}
            at={at.x+kDirX[dir],at.y+kDirY[dir]};path.push_back(at);
        }
        if(at!=nodes[to].r.pos)path.clear();
    }
    std::vector<std::array<CandidateAudit,3>> best(n);
    std::vector<std::array<bool,3>> found(n);
    std::array<int,3> order{};
    auto evaluate=[&](int depth,int total) {
        std::set<Point> visited;std::set<std::string> collected;std::vector<RoutePickup> pickups;
        int time=0,from=n;double confidence=1;
        for(int k=0;k<depth;++k) {
            int to=order[k];const auto& path=paths[from][to];
            if(path.empty()) {
                // Distant singleton remains eligible, with no invented intermediate pickups.
                if(depth!=1)return;
                time=nodes[to].d;visited.insert(nodes[to].r.pos);
                if(hazards&&(hazards->lethalAt(nodes[to].r.pos,p.tick+time,false)||hazards->blockedAt(nodes[to].r.pos,p.tick+time)||hazards->damageAt(nodes[to].r.pos,p.tick+time,false)>=p.hp))return;
                if(nodes[to].r.value>0)pickups.push_back({nodes[to].r,time,nodes[to].rival,nodes[to].choice});
            } else for(Point at:path) {
                ++time;visited.insert(at);
                if(hazards&&(hazards->lethalAt(at,p.tick+time,false)||hazards->blockedAt(at,p.tick+time)||hazards->damageAt(at,p.tick+time,false)>=p.hp))return;
                for(const auto& item:mem.resources())if(item.second.pos==at && collected.insert(item.first).second) {
                    auto [rival,choice]=contest(item.second,time);
                    pickups.push_back({item.second,time,rival,choice});
                }
            }
            from=to;
        }
        if(time!=total)return;
        // Upper bound from official resource count; protects malformed memories.
        if(pickups.size()>16)return;
        auto score=weighted_route_score(p,mem.normalCollected(),mem.highCollected(),pickups,visited);
        CandidateAudit c;int first=order[0];c.kind=nodes[first].r.value>0?"resource_plan":"task_plan";
        c.id=nodes[first].r.id;c.target=nodes[first].r.pos;c.eta=total;c.stops=depth;c.rivalEta=nodes[first].rival;c.rivalCommitment=nodes[first].choice;
        c.age=nodes[first].r.observedTick<0?0:std::max(0,p.tick-nodes[first].r.observedTick);
        int rawValue=0,normal=0,high=0;
        for(const auto& x:pickups){rawValue+=x.resource.value;(x.resource.value>=20?high:normal)++;c.routeIds.push_back(x.resource.id);c.routeEtas.push_back(x.eta);int age=x.resource.observedTick<0?0:std::max(0,p.tick-x.resource.observedTick);confidence=std::min(confidence,choice_confidence(x.eta,x.rival,age,x.commitment));}
        c.confidence=confidence;c.score=score;c.nominalGain=route_outcome(p,mem.normalCollected(),mem.highCollected(),rawValue,normal,high,visited).points();
        c.priority=score.utility()/(total+1.0);
        c.lostIfDelayed=std::max(0.0,score.points()-weighted_route_score(p,mem.normalCollected(),mem.highCollected(),pickups,visited,1).points());
        // Keep a singleton and alternatives with/without coordinate completion.
        // Otherwise pre-ranking can discard a useful plan before Bot applies
        // the explicit recoverable-coordinate auxiliary.
        int bucket=depth==1?0:score.reachTaskConditional>0?2:1;
        if(!found[first][bucket]||c.priority>best[first][bucket].priority){best[first][bucket]=std::move(c);found[first][bucket]=true;}
    };
    for(int a=0;a<n&&out.expansions<2000;++a){
        order[0]=a;evaluate(1,nodes[a].d);++out.expansions;
        for(int b=0;b<n&&out.expansions<2000;++b)if(b!=a){int gap=reverse[b].at(nodes[a].r.pos),t=nodes[a].d+gap;if(gap<=0||t>std::min(24,remaining))continue;order[1]=b;evaluate(2,t);++out.expansions;
            for(int c=0;c<n&&out.expansions<2000;++c)if(c!=a&&c!=b){int d=reverse[c].at(nodes[b].r.pos),end=t+d;if(d<=0||end>std::min(24,remaining))continue;order[2]=c;evaluate(3,end);++out.expansions;}
        }
    }
    for(int i=0;i<n;++i)for(int k=0;k<3;++k)if(found[i][k])out.goals.push_back(std::move(best[i][k]));
    return out;
}
} // namespace rmtc9
