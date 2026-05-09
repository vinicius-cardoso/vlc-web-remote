#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
APP_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
DESKTOP_ID="vlc-control-vlc.desktop"
DESKTOP_FILE="$APP_DIR/$DESKTOP_ID"
SYSTEM_VLC_DESKTOP="/usr/share/applications/vlc.desktop"

if [[ ! -f "$SYSTEM_VLC_DESKTOP" ]]; then
  echo "Nao encontrei $SYSTEM_VLC_DESKTOP. Instale o VLC via APT primeiro."
  exit 1
fi

MIME_TYPES="$(awk -F= '/^MimeType=/{ print $2; exit }' "$SYSTEM_VLC_DESKTOP")"

mkdir -p "$APP_DIR"

{
  printf '%s\n' "[Desktop Entry]"
  printf '%s\n' "Version=1.0"
  printf '%s\n' "Type=Application"
  printf '%s\n' "Name=VLC media player"
  printf '%s\n' "GenericName=Media player"
  printf '%s\n' "Comment=Read, capture, broadcast your multimedia streams"
  printf 'Exec=%s/scripts/start-vlc.sh --started-from-file %%U\n' "$PROJECT_DIR"
  printf 'TryExec=%s/scripts/start-vlc.sh\n' "$PROJECT_DIR"
  printf '%s\n' "Icon=vlc"
  printf '%s\n' "Terminal=false"
  printf '%s\n' "Categories=AudioVideo;Player;Recorder;"
  printf '%s\n' "StartupNotify=false"
  printf '%s\n' "StartupWMClass=vlc"
  printf '%s\n' "Keywords=Player;Capture;DVD;Audio;Video;Server;Broadcast;"
  printf 'MimeType=%s\n' "$MIME_TYPES"
} > "$DESKTOP_FILE"

chmod 0644 "$DESKTOP_FILE"

if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$APP_DIR" >/dev/null 2>&1 || true
fi

if command -v xdg-mime >/dev/null 2>&1; then
  IFS=';' read -r -a mime_types <<< "$MIME_TYPES"
  for mime_type in "${mime_types[@]}"; do
    if [[ -n "$mime_type" ]]; then
      xdg-mime default "$DESKTOP_ID" "$mime_type" || true
    fi
  done
fi

echo "Entrada instalada: $DESKTOP_FILE"
echo "Videos agora podem abrir com VLC pelo menu grafico usando HTTP ativo."
