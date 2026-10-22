#!/usr/bin/env python3
"""Generate reproducible TEST maps, never runtime inputs for the contestant bot."""
import argparse
from collections import deque
import json
from pathlib import Path
import random

ROOT = Path(__file__).resolve().parents[1]

def make(name, w, h, spawn, walls, resources, minimum=None, radius=4):
    walls = sorted(set(walls))
    assert tuple(spawn) not in walls
    assert all(0 <= x < w and 0 <= y < h for x, y in walls)
    assert all(tuple(p) not in walls for p, value in resources)
    config = dict(stage=1, vision_radius=radius, respawn_wait=3, spawn_protection=2,
                  mission_text='练习采集：自主收集资源。', robots=[dict(team='A', spawn=spawn)], missions=[],
                  map=dict(width=w, height=h, obstacles=walls,
                           resources=[dict(id=f'r{i}', position=p, value=v) for i, (p, v) in enumerate(resources)]),
                  events=dict(enabled=False, max_hp=100, base_attack_damage=20, base_defended_damage=10))
    return dict(name=name, minimum_value=minimum, config=config)

def suite(seed_base=20261022, random_count=40):
    maps = [
        make('visible_detour', 10, 10, [1, 4], [(3, y) for y in range(1, 8)], [([5, 4], 20)], 20),
        make('unseen_far_corner', 20, 20, [0, 0], [], [([19, 19], 20)], 20),
        make('sealed_high_resource', 12, 12, [1, 1], [(5, 6), (7, 6), (6, 5), (6, 7)],
             [([6, 6], 20), ([2, 1], 10), ([10, 10], 10)], 20),
        make('disconnected_rooms', 12, 12, [1, 1], [(6, y) for y in range(12)],
             [([4, 10], 10), ([10, 1], 20)], 10),
        make('long_corridor', 24, 8, [0, 0], [(x, y) for x in range(24) for y in range(1, 8)],
             [([23, 0], 20)], 20),
        make('narrow_view', 12, 12, [0, 0], [], [([11, 11], 20)], 20, radius=1),
        make('zero_view_probe', 8, 8, [0, 0], [], [([3, 0], 20)], 20, radius=0),
        make('empty_map', 12, 12, [11, 11], [], [], 0),
        make('budget_unreachable', 128, 8, [0, 0], [], [([127, 0], 20)], 0),
        make('rectangular_bottom_right', 28, 10, [27, 9], [], [([0, 0], 20), ([26, 9], 10)], 30),
        make('spawn_resource', 8, 8, [0, 0], [], [([0, 0], 20)], 20),
    ]
    for corner, spawn in enumerate(([0, 0], [19, 0], [0, 19], [19, 19])):
        maps.append(make(f'corner_{corner}', 20, 20, spawn, [],
                         [([3, 3], 20), ([16, 3], 20), ([3, 16], 20), ([16, 16], 20)], 80))
    for seed in range(random_count):
        rng = random.Random(seed_base + seed)
        w, h = rng.choice([(12, 12), (20, 20), (24, 24), (28, 12)])
        spawn = [rng.randrange(w), rng.randrange(h)]
        density = (0.05, 0.15, 0.25, 0.35)[seed % 4]
        walls = [(x, y) for y in range(h) for x in range(w)
                 if [x, y] != spawn and rng.random() < density]
        wallset = set(walls)
        free = [(x, y) for y in range(h) for x in range(w) if (x, y) not in wallset and [x, y] != spawn]
        points = rng.sample(free, 16)
        case = make(f'random_{seed:02d}', w, h, spawn, walls,
                    [(p, 10 if i < 12 else 20) for i, p in enumerate(points)])
        # Test-only reachability oracle sets a modest collection floor. It is
        # never supplied to BotRuntime and is not an optimal 100-tick tour score.
        seen = {tuple(spawn)}
        queue = deque(seen)
        while queue:
            x, y = queue.popleft()
            for nx, ny in ((x-1, y), (x+1, y), (x, y-1), (x, y+1)):
                if 0 <= nx < w and 0 <= ny < h and (nx, ny) not in wallset and (nx, ny) not in seen:
                    seen.add((nx, ny))
                    queue.append((nx, ny))
        reachable_value = sum(r['value'] for r in case['config']['map']['resources'] if tuple(r['position']) in seen)
        case['minimum_value'] = min(100, reachable_value)
        case['seed'] = seed_base + seed
        maps.append(case)
    return dict(schema_version=1, max_ticks=100, cases=maps)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'rmtc_ws/src/rmtc_bot/test/maps/stage1_suite.json')
    parser.add_argument('--seed-base', type=int, default=20261022)
    parser.add_argument('--random-count', type=int, default=40)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(suite(args.seed_base, args.random_count), ensure_ascii=False, indent=2) + '\n')
    print(f'Wrote {args.output}')
