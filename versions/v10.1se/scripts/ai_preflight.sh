#!/usr/bin/env bash
# AI 网关赛前检查：只读检查，不打印令牌。
#
# 用法：
#   export RMTC_AI_GATEWAY='http://<裁判机IP>:8000'
#   export RMTC_TEAM_TOKEN='<本场令牌>'
#   unset RMTC_MODEL
#   bash scripts/ai_preflight.sh            # 只检查 /healthz，不消耗配额
#   bash scripts/ai_preflight.sh --spend    # 再发一次对话请求（成功会消耗 1 次配额）
#
# 注意：正式资格以裁判/网关记录为准；本脚本只帮助确认地址、令牌和网络可用。
set -euo pipefail

SPEND=0
if [[ "${1:-}" == "--spend" ]]; then
  SPEND=1
elif [[ $# -gt 0 ]]; then
  echo "usage: bash scripts/ai_preflight.sh [--spend]" >&2
  exit 2
fi

if [[ -z "${RMTC_AI_GATEWAY:-}" ]]; then
  echo "ERROR: RMTC_AI_GATEWAY is empty" >&2
  exit 2
fi
if [[ -z "${RMTC_TEAM_TOKEN:-}" ]]; then
  echo "ERROR: RMTC_TEAM_TOKEN is empty" >&2
  exit 2
fi
if [[ -n "${RMTC_MODEL:-}" ]]; then
  echo "ERROR: RMTC_MODEL must be empty (official gateway chooses the default model)" >&2
  exit 2
fi

BASE="${RMTC_AI_GATEWAY%/}"
case "$BASE" in
  */v1/chat/completions) CHAT="$BASE" ;;
  */v1)                  CHAT="$BASE/chat/completions" ;;
  *)                     CHAT="$BASE/v1/chat/completions" ;;
esac

TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT

echo "healthz: $BASE/healthz"
CODE="$(curl -sS --max-time 5 -o "$TMP" -w '%{http_code}' "$BASE/healthz" 2>/dev/null || true)"
echo "  HTTP ${CODE:-000}"
if [[ "${CODE:-000}" == "000" ]]; then
  echo "  cannot reach the gateway; check address, network and firewall" >&2
  exit 3
fi
if [[ "${CODE:-000}" != "200" ]]; then
  # The official protocol documents /healthz, but the practice referee does not
  # implement it. A non-200 here is not fatal; the chat request is authoritative.
  echo "  warning: healthz is not 200; continuing to the chat check" >&2
fi

if [[ "$SPEND" != "1" ]]; then
  echo "OK: gateway responded; no quota spent."
  echo "Next: bash scripts/ai_preflight.sh --spend   # one chat request, consumes quota if 2xx"
  exit 0
fi

echo "chat: $CHAT (one request; a 2xx consumes quota)"
CODE="$(curl -sS --max-time 25 -o "$TMP" -w '%{http_code}' \
  -H "Authorization: Bearer $RMTC_TEAM_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"messages":[{"role":"user","content":"Only reply with {\"events\":[]}"}],"max_tokens":64,"stream":false}' \
  "$CHAT" 2>/dev/null || true)"
echo "  HTTP ${CODE:-000}"

if [[ "${CODE:-000}" != "200" ]]; then
  echo "  401 -> token wrong/expired/seat mismatch" >&2
  echo "  404 -> gateway path wrong" >&2
  echo "  429 quota_exceeded -> stop and ask the referee" >&2
  echo "  502/504 -> upstream/network problem" >&2
  echo "  503 audit_unavailable -> ask the referee to verify the call record" >&2
  exit 4
fi

python3 - "$TMP" <<'PY'
import json
import sys

path = sys.argv[1]
try:
    data = json.load(open(path, encoding="utf-8"))
except Exception as exc:
    print("  response is not JSON: %s" % exc, file=sys.stderr)
    sys.exit(5)
try:
    content = data["choices"][0]["message"]["content"]
except Exception:
    print("  no choices[0].message.content; keys=%s" % sorted(data), file=sys.stderr)
    sys.exit(5)
if not content:
    print("  empty content: HTTP 2xx still counts for qualification, but no plan", file=sys.stderr)
    sys.exit(0)
print("  content:", content[:200].replace("\n", " "))
print("OK: 2xx received for this seat/session; save this output and the bot log.")
PY
