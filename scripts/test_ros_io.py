#!/usr/bin/env python3
"""Local synthetic ROS wire test. Run in an unused ROS domain after building."""
import json
import os
from pathlib import Path
import signal
import subprocess
import time

import rclpy
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
from std_msgs.msg import String

ROOT = Path(__file__).resolve().parents[1]

def main():
    rclpy.init()
    node = rclpy.create_node('rmtc_wire_test')
    qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE,
                     durability=DurabilityPolicy.VOLATILE)
    received = []
    node.create_subscription(String, '/team_A/action', lambda m: received.append(json.loads(m.data)), qos)
    publisher = node.create_publisher(String, '/team_A/perception', qos)
    env = dict(os.environ, RMTC_LLM_ENABLED='0')
    process = subprocess.Popen([
        str(ROOT / 'rmtc_ws/install/rmtc_bot/lib/rmtc_bot/rmtc_bot'),
        '--ros-args', '-p', 'team:=A', '-p', 'stage:=1', '-p', 'llm_enabled:=false',
        '-p', 'max_ticks:=100'], env=env, stdout=subprocess.DEVNULL)

    def pump(seconds):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            rclpy.spin_once(node, timeout_sec=0.02)

    def send(frame):
        publisher.publish(String(data=json.dumps(frame)))

    def expect(frame, count):
        send(frame)
        until = time.monotonic() + 3
        while len(received) < count and time.monotonic() < until:
            rclpy.spin_once(node, timeout_sec=0.02)
        assert len(received) == count, (len(received), count)
        assert received[-1]['tick'] == frame['tick']
        assert received[-1]['session_id'] == frame['session_id']

    try:
        until = time.monotonic() + 8
        while publisher.get_subscription_count() == 0 and time.monotonic() < until:
            pump(0.1)
        assert publisher.get_subscription_count() == 1, 'Use an isolated ROS domain with exactly one bot'
        pump(0.5)
        frame = json.loads((ROOT / 'rmtc_ws/src/rmtc_bot/test/fixtures/perception_stage1.json').read_text())
        frame['session_id'] = 'wire-test-first'
        expect(frame, 1)
        assert received[-1]['action'] == 'move' and received[-1]['direction'] == 'right'
        # Pause/resume can reopen the same tick: resubmit exactly the cached action.
        original = dict(received[-1])
        for count in range(2, 5):
            expect(frame, count)
            assert received[-1] == original
        # Feed the executed action back via the public receipt and actual position.
        frame['tick'] += 1
        frame['self']['last_action'] = dict(received[-1], moved=True, effective_direction='right')
        frame['self']['position'] = [3, 2]
        frame['visible_entities'] = []
        frame['score']['resource_value'] = 20
        expect(frame, 5)
        malformed = dict(frame, tick=2, vision_radius=9)
        send(malformed)
        pump(0.2)
        assert len(received) == 5
        frame['tick'] = 0
        send(frame)
        pump(0.2)
        assert len(received) == 5
        frame['session_id'] = 'wire-test-second'
        frame['self']['alive'] = False
        frame['self']['hp'] = 0
        expect(frame, 6)
        assert received[-1]['action'] == 'wait' and 'direction' not in received[-1]
        frame['tick'] = 100
        send(frame)
        pump(0.2)
        assert len(received) == 6
        print('PASS ROS wire: context, move, same-tick cached resubmission, receipt input, malformed/stale frames, new session, dead wait, final tick')
    finally:
        process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        node.destroy_node()
        rclpy.shutdown()

if __name__ == '__main__':
    main()
