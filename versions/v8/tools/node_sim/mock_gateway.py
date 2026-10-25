#!/usr/bin/env python3
"""tools/node_sim/mock_gateway.py —— 本地假 AI 网关（只用 Python 标准库）

模拟统一网关的公开接口：GET /healthz、POST /v1/chat/completions（Bearer 令牌）。
它不是模型：从请求里的 announcements 找出事件编号（离线裁判的公告形如“事件预告 ev3”），
再从 node_sim_events.json 查出对应的结构化事件返回，用来端到端检查节点的
“发请求 → 收结果 → 解析 → 用于决策”链路。

环境变量：
  MOCK_PORT        监听端口（默认 18080）
  MOCK_LATENCY_MS  每次响应前等待的毫秒数（默认 150，用来模拟模型耗时）
  MOCK_FAIL_EVERY  每 N 次请求返回一次 502（默认 0 = 不注入故障）
  MOCK_TOKEN       期望的令牌（默认 sim-token）
  MOCK_LOG         调用记录文件（默认 node_sim_calls.jsonl）
"""
import json
import os
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(os.environ.get("MOCK_PORT", "18080"))
LATENCY = int(os.environ.get("MOCK_LATENCY_MS", "150")) / 1000.0
FAIL_EVERY = int(os.environ.get("MOCK_FAIL_EVERY", "0"))
TOKEN = os.environ.get("MOCK_TOKEN", "sim-token")
LOG = os.environ.get("MOCK_LOG", "node_sim_calls.jsonl")
EVENTS = os.environ.get("MOCK_EVENTS", "node_sim_events.json")
lock = threading.Lock()
count = 0


def answer(request_text):
    try:
        body = json.loads(request_text)
        user = json.loads(body["messages"][-1]["content"])
        lines = user.get("request", {}).get("announcements", [])
    except Exception:
        return '{"events":[]}'
    ids = set()
    for line in lines:
        ids.update(re.findall(r"\bev\d+\b", line))
    try:
        with open(EVENTS, encoding="utf-8") as f:
            table = json.load(f).get("events", [])
    except Exception:
        table = []
    picked = [e for e in table if e.get("id", "").split("_")[0] in ids]
    return json.dumps({"events": picked}, ensure_ascii=False)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, status, payload):
        data = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/healthz":
            self.reply(200, {"status": "ok"})
        else:
            self.reply(404, {"error": {"code": "not_found"}})

    def do_POST(self):
        global count
        if self.path != "/v1/chat/completions":
            return self.reply(404, {"error": {"code": "not_found"}})
        if self.headers.get("Transfer-Encoding"):
            return self.reply(501, {"error": {"code": "unsupported_transfer_encoding"}})
        if self.headers.get("Authorization") != "Bearer " + TOKEN:
            return self.reply(401, {"error": {"code": "invalid_token"}})
        length = int(self.headers.get("Content-Length", "0"))
        text = self.rfile.read(length).decode("utf-8", "replace")
        with lock:
            count += 1
            n = count
        time.sleep(LATENCY)
        tick = None
        try:
            tick = json.loads(json.loads(text)["messages"][-1]["content"])["request"]["tick"]
        except Exception:
            pass
        if FAIL_EVERY and n % FAIL_EVERY == 0:
            status, payload = 502, {"error": {"code": "upstream_unavailable"}}
        else:
            status = 200
            payload = {"choices": [{"message": {"role": "assistant", "content": answer(text)}}]}
        with lock, open(LOG, "a", encoding="utf-8") as f:
            f.write(json.dumps({"n": n, "tick": tick, "status": status}) + "\n")
        self.reply(status, payload)


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
