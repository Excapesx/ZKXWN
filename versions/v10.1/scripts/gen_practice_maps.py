#!/usr/bin/env python3
"""生成"练习包几何"的第二阶段配置，供离线测评使用。

为什么需要这个脚本：
  规则 v1.1 §4.2/§12：标准地图是第一阶段 20x20、第二阶段 **24x24**，实际尺寸随感知提供。
  官方练习包里的 stage2.json 是 18x18 的**练习**图：4 个角落出生点、16 个资源
  （12 普通 + 4 高价值），4 个高价值资源分别落在四个象限（到各出生点曼哈顿距离相同），
  到达任务在地图正中，整张图四重对称。

  之前 10.1 的离线测评用 24x24 + 13% 随机墙 + 资源随机撒点。**尺寸对了（24x24），
  但版式是随机的**：练习包版式下机器人一出生就看得见自己象限的高价值资源，
  随机版式下它常常在扫到之前资源就被别人拿走了（随机图 mission_high 只有 0.72）。
  这个脚本按练习包的版式（对称、每象限一个高价值、到达任务在正中）生成 24x24 地图，
  这才是标准地图最可能的形状。

用法:
  python3 scripts/gen_practice_maps.py <输出目录> [张数] [种子] [18|24]
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


def build(seed, events, size=24):
    rnd = random.Random(seed)
    n = size
    last = n - 1
    mid = n // 2

    def mirror(x):
        return last - x

    # 四个象限各一个高价值资源：四向镜像，到各出生点距离相同。
    # 练习包 18x18 的高价值资源恰好在 (5,5) = 出生点偏移 (mid//2, mid//2)，
    # 这里沿用同样的半径结构（24x24 -> (7,7)），只做小幅抖动。
    def off(delta):
        return rnd.randint(mid // 2 - delta, mid // 2 + delta)

    ox, oy = off(1), off(1)
    highs = [[1 + ox, 1 + oy], [mirror(1 + ox), 1 + oy], [1 + ox, mirror(1 + oy)], [mirror(1 + ox), mirror(1 + oy)]]

    # 每个象限四个普通资源，结构同样对齐练习包：
    #   角点 (1,0)、边上 (4,1)、内圈 (4,mid-2)、内圈 (mid-2,4)。
    normals = []
    for (bx, by) in ((1, 0), (4, 1), (4, mid - 2), (mid - 2, 4)):
        nx, ny = 1 + bx, 1 + by
        if rnd.random() < 0.5:
            nx, ny = ny, nx
        for p in ([[nx, ny], [mirror(nx), ny], [nx, mirror(ny)], [mirror(nx), mirror(ny)]]):
            if p not in normals and p not in highs:
                normals.append(p)
    while len(normals) < 12:
        cand = [rnd.randint(1, last - 1), rnd.randint(1, last - 1)]
        if cand not in normals and cand not in highs:
            normals.append(cand)
    normals = normals[:12]

    resources = []
    for i, p in enumerate(normals):
        resources.append({"id": "n%d" % i, "position": p, "value": 10})
    for i, p in enumerate(highs):
        resources.append({"id": "h%d" % i, "position": p, "value": 20})

    spawns = [[1, 1], [last - 1, 1], [1, last - 1], [last - 1, last - 1]]
    taken = {tuple(r["position"]) for r in resources}
    taken |= {tuple(s) for s in spawns}
    obstacles = []
    guard = 0
    while len(obstacles) < 8 and guard < 600:
        guard += 1
        x = rnd.randint(1, last - 1)
        y = rnd.randint(1, last - 1)
        if (x, y) in taken:
            continue
        if any(abs(x - sx) + abs(y - sy) < 3 for sx, sy in spawns):
            continue
        obstacles.append([x, y])
        taken.add((x, y))

    return {
        "stage": 2,
        "vision_radius": 4,
        "respawn_wait": 3,
        "spawn_protection": 2,
        "mission_text": "练习任务：收集两个普通资源、一个高价值资源，并到达 [%d,%d]。" % (mid, mid),
        "robots": [{"team": c, "spawn": s} for c, s in zip("ABCD", spawns)],
        "missions": [
            {"id": "collect_two_normal", "type": "normal_resources", "target": 2},
            {"id": "collect_high_value", "type": "high_resources", "target": 1},
            {"id": "reach_target", "type": "reach", "position": [mid, mid]},
        ],
        "map": {"width": n, "height": n, "obstacles": obstacles, "resources": resources},
        "events": events,
    }


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    outdir = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 64
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 20261028
    size = int(sys.argv[4]) if len(sys.argv) > 4 else 24
    events = load_official_events()
    if events is None:
        print("warning: %s not found, generating without events" % PRACTICE_ZIP)
        events = {"enabled": False}
    os.makedirs(outdir, exist_ok=True)
    for i in range(count):
        cfg = build(seed + i * 7919, events, size)
        with open(os.path.join(outdir, "stage2_%03d.json" % i), "w", encoding="utf-8") as f:
            json.dump(cfg, f, ensure_ascii=False)
    print("wrote %d %dx%d practice-geometry configs to %s" % (count, size, size, outdir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
