#!/usr/bin/env bash
# Interactive contest launcher. Credentials remain in process memory only.
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
read -r -p 'Official ROS_DOMAIN_ID: ' ROS_DOMAIN_ID
if [[ ! "$ROS_DOMAIN_ID" =~ ^[0-9]{1,3}$ ]] || ((10#$ROS_DOMAIN_ID > 232)); then
  echo 'Invalid domain: enter organizer-provided integer 0..232.' >&2; exit 1
fi
ROS_DOMAIN_ID=$((10#$ROS_DOMAIN_ID))
read -r -p 'Assigned seat (A/B/C/D): ' SEAT
[[ "$SEAT" =~ ^[ABCD]$ ]] || { echo 'Invalid seat.' >&2; exit 1; }
read -r -p 'Stage (1/2): ' STAGE
[[ "$STAGE" =~ ^[12]$ ]] || { echo 'Invalid stage.' >&2; exit 1; }
if [[ "$STAGE" == 1 && "$SEAT" != A ]]; then
  echo 'Stage 1 uses seat A only; restart with the organizer-assigned settings.' >&2; exit 1
fi
unset RMTC_MODEL RMTC_AI_GATEWAY RMTC_TEAM_TOKEN RMTC_DECISION_CONFIG RMTC_MAX_TICKS
if [[ "$STAGE" == 2 ]]; then
  read -r -p 'Official gateway URL (http://... or https://...): ' RMTC_AI_GATEWAY
  [[ "$RMTC_AI_GATEWAY" =~ ^https?://[^[:space:]]+$ ]] || { echo 'Invalid gateway URL.' >&2; exit 1; }
  read -r -s -p 'Current match token (hidden): ' RMTC_TEAM_TOKEN
  printf '\n'
  [[ -n "$RMTC_TEAM_TOKEN" && "$RMTC_TEAM_TOKEN" != *$'\r'* && "$RMTC_TEAM_TOKEN" != *$'\n'* ]] || { echo 'Invalid token.' >&2; exit 1; }
  export RMTC_AI_GATEWAY RMTC_TEAM_TOKEN
fi
# Use the organizer's wired network directly, independently of desktop VPN settings.
unset HTTP_PROXY HTTPS_PROXY ALL_PROXY http_proxy https_proxy all_proxy
export NO_PROXY='*' no_proxy='*'
export ROS_DOMAIN_ID ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export RMTC_TEAM_NAME="${RMTC_TEAM_NAME:-HERO-10.1.1}" RMTC_MAX_TOKENS=512 RMTC_HTTP_TIMEOUT_MS=15000
export RMTC_DECISION_CONFIG="$ROOT/decision_config.json"
python3 "$ROOT/scripts/verify_delivery.py"
if [[ "$STAGE" == 2 ]]; then python3 "$ROOT/scripts/verify_delivery.py" --require-llm; fi
echo "Launching HERO 10.1.1, seat=$SEAT, stage=$STAGE, domain=$ROS_DOMAIN_ID. Ctrl+C stops this bot."
exec bash "$ROOT/scripts/run.sh" "$SEAT" "$STAGE"
