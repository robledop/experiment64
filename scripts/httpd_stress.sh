#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${1:-http://localhost:8080}"
OUT_DIR="${2:-./stress_logs}"
DURATION="${DURATION:-30s}"

mkdir -p "$OUT_DIR"

log() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

if ! have curl; then
  echo "curl is required for warmup/fallback."
  exit 1
fi

sanitize() {
  local s="$1"
  s="${s#/}"
  s="${s//\//_}"
  if [[ -z "$s" ]]; then s="root"; fi
  printf '%s' "$s"
}

ENDPOINTS=("/" "/big.bin" "/nope")

log "Warmup..."
curl -fsS -H 'Connection: close' "${BASE_URL}/" >/dev/null || true

if have wrk; then
  for ep in "${ENDPOINTS[@]}"; do
    ep_name="$(sanitize "$ep")"
    for tc in "4 64" "4 128"; do
      t="${tc%% *}"
      c="${tc##* }"
      log "wrk ${t}t ${c}c ${DURATION} ${ep}"
      wrk -t"$t" -c"$c" -d"$DURATION" --latency -H 'Connection: close' "${BASE_URL}${ep}" \
        > "${OUT_DIR}/wrk_${ep_name}_${t}t_${c}c.txt" || true
    done
  done
else
  log "wrk not found, skipping."
fi

if have hey; then
  for ep in "${ENDPOINTS[@]}"; do
    ep_name="$(sanitize "$ep")"
    for c in 10 50 100; do
      log "hey ${c}c ${DURATION} ${ep}"
      hey -z "$DURATION" -c "$c" -H 'Connection: close' "${BASE_URL}${ep}" \
        > "${OUT_DIR}/hey_${ep_name}_${c}c.txt" || true
    done
  done
else
  log "hey not found, skipping."
fi

if have ab; then
  for ep in "${ENDPOINTS[@]}"; do
    ep_name="$(sanitize "$ep")"
    log "ab 10000 req, 100c ${ep}"
    ab -n 10000 -c 100 -H 'Connection: close' "${BASE_URL}${ep}" \
      > "${OUT_DIR}/ab_${ep_name}_100c.txt" || true
  done
else
  log "ab not found, skipping."
fi

# Fallback if none of the load tools exist
if ! have wrk && ! have hey && ! have ab; then
  log "No load tool found. Using curl+xargs fallback..."
  for ep in "${ENDPOINTS[@]}"; do
    ep_name="$(sanitize "$ep")"
    seq 1 2000 | xargs -n1 -P 50 -I{} \
      curl -fsS -H 'Connection: close' "${BASE_URL}${ep}" >/dev/null || true
    log "curl fallback done for ${ep}"
  done
fi

log "Done. Logs in ${OUT_DIR}"