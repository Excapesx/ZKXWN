#!/usr/bin/env python3
"""生成"练习包几何"的第二阶段配置，供离线测评使用。

为什么需要这个脚本：
  官方练习包 `03_官方练习包_v1.1/dist.zip` 里的 stage2.json 是 18x18、只有 6 个障碍、
  4 个角落出生点 [1,1][16,1][1,16][16,16]、16 个资源（12 普通 + 4 高价值），并且
  4 个高价值资源分别落在四个象限 [5,5][12,5][5,12][12,12]（到各出生点曼哈顿距离都是 8），
  到达任务是地图正中 [9,9]。整张图是四重对称的。

  之前 10.1 的离线测评一直用 24x24、13% 随机墙、资源随机撒点的地图。那种地图和正式
  地图的几何完全不同：随机地图上资源要到 Tick~48 才会被采光，练习包几何上 Tick~14 就
  采光了。在随机地图上调出来的权重，不代表正式地图上的行为。

本脚本按练习包的结构生成多张同构地图，这样 A/B 对比才有统计意义。
输出是官方练习包同样的 JSON 结构，tools/bench/referee.cpp 可以直接 --practice 加载。

用法:
  python3 scripts/gen_practice_maps.py <输出目录> [张数] [种子]
"""
import io
import json
import os
import random
import sys
import tarfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PRACTICE_ZIP = os.path.join(ROOT, "03_官方练习包_v1.1", "dist.zip")


def load_official_events():
    """从官方练习包里取出事件定义/时间表，保持与正式事件一致的参数。"""
    if not os.path.exists(PRACTICE_ZIP):
        return None
    with zipfile.ZipFile(PRACTICE_ZIP) as z:
        name = next(n for n in z.namelist() if n.endswith("rmtc_practice.tar.gz"))
        raw = z.read(name)
    with tarfile.open(fileobj=io.BytesIO(raw)) as t:
        member = next(m for m in t.getmembers() if m.name.endswith("config/stage2.json"))
        cfg = json.loads(t.extractfile(member).read().decode("utf-8"))
    return cfg["events"]


def mirror(x, n=18):
    return n - 1 - x


def build(seed, events):
    rnd = random.Random(seed)
    W = H = 18

    # 每个象限一个高价值资源，四向镜像，整张图保持四重对称。
    hx = rnd.randint(3, 7)
    hy = rnd.randint(3, 7)
    highs = [[hx, hy], [mirror(hx), hy], [hx, mirror(hy)], [mirror(hx), mirror(hy)]]

    # 每象限三个普通资源，同样四向镜像。
    normals = []
    for _ in range(3):
        nx = rnd.randint(1, 8)
        ny = rnd.randint(1, 8)
        for p in ([[nx, ny], [mirror(nx), ny], [nx, mirror(ny)], [mirror(nx), mirror(ny)]]):
            if p not in normals and p not in highs:
                normals.append(p)
    # 不足时用练习包自己的角点/边/内圈固定点位补齐。
    fixed = [[2, 1], [15, 1], [1, 15], [16, 15],
             [4, 2], [13, 2], [2, 13], [15, 13],
             [4, 8], [13, 9], [8, 4], [9, 13]]
    for p in fixed:
        if len(normals) >= 12:
            break
        if p not in normals and p not in highs:
            normals.append(p)
    while len(normals) < 12:
        normals.append(fixed[len(normals) % 12])

    resources = []
    for i, p in enumerate(normals[:12]):
        resources.append({"id": "n%d" % i, "position": p, "value": 10})
    for i, p in enumerate(highs):
        resources.append({"id": "h%d" % i, "position": p, "value": 20})

    taken = {tuple(r["position"]) for r in resources}
    taken |= {(1, 1), (16, 1), (1, 16), (16, 16)}
    obstacles = []
    guard = 0
    while len(obstacles) < 6 and guard < 400:
        guard += 1
        x = rnd.randint(1, 16)
        y = rnd.randint(1, 16)
        if (x, y) in taken:
            continue
        if any(abs(x - sx) + abs(y - sy) < 3
               for sx, sy in [(1, 1), (16, 1), (1, 16), (16, 16)]):
            continue
        obstacles.append([x, y])
        taken.add((x, y))

    return {
        "stage": 2,
        "vision_radius": 4,
        "respawn_wait": 3,
        "spawn_protection": 2,
        "mission_text": "练习任务：收集两个普通资源、一个高价值资源，并到达 [9,9]。",
        "robots": [
            {"team": "A", "spawn": [1, 1]},
            {"team": "B", "spawn": [16, 1]},
            {"team": "C", "spawn": [1, 16]},
            {"team": "D", "spawn": [16, 16]},
        ],
        "missions": [
            {"id": "collect_two_normal", "type": "normal_resources", "target": 2},
            {"id": "collect_high_value", "type": "high_resources", "target": 1},
            {"id": "reach_target", "type": "reach", "position": [9, 9]},
        ],
        "map": {"width": W, "height": H, "obstacles": obstacles, "resources": resources},
        "events": events,
    }


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    outdir = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 64
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 20261028
    events = load_official_events()
    if events is None:
        print("warning: %s not found, generating without events" % PRACTICE_ZIP)
        events = {"enabled": False}
    os.makedirs(outdir, exist_ok=True)
    for i in range(count):
        cfg = build(seed + i * 7919, events)
        with open(os.path.join(outdir, "stage2_%03d.json" % i), "w", encoding="utf-8") as f:
            json.dump(cfg, f, ensure_ascii=False)
    print("wrote %d practice-geometry configs to %s" % (count, outdir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
