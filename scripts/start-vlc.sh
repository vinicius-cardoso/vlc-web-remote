#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

if [[ -f "$PROJECT_DIR/.env" ]]; then
  set -a
  # shellcheck disable=SC1091
  source "$PROJECT_DIR/.env"
  set +a
fi

PASSWORD="${VLC_PASSWORD:-vlc}"
HTTP_HOST="${VLC_HTTP_HOST:-127.0.0.1}"
HTTP_PORT="${VLC_PORT:-8080}"
VLC_BIN="${VLC_BIN:-}"

if [[ -z "$VLC_BIN" ]]; then
  if [[ -x /usr/bin/vlc ]]; then
    VLC_BIN="/usr/bin/vlc"
  else
    VLC_BIN="$(command -v vlc)"
  fi
fi

if [[ "${VLC_FORCE_SOFTWARE_GL:-0}" == "1" ]]; then
  export LIBGL_ALWAYS_SOFTWARE=1
fi

exec "$VLC_BIN" \
  --extraintf http \
  --http-password "$PASSWORD" \
  --http-host "$HTTP_HOST" \
  --http-port "$HTTP_PORT" \
  "$@"
