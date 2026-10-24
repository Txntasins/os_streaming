#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."
ROOT_DIR="$(pwd)"
ACTION="${1:-start}"
PORT="${2:-8081}"

mkdir -p media
for f in ../www/*.mp4; do
  [ -f "$f" ] && [ ! -f "media/$(basename "$f")" ] && cp -f "$f" media/
done

python3 scripts/sync_catalog.py

CONF_RUN="/tmp/os_streaming_nginx_run.conf"
sed -e "s|__ROOT_DIR__|$ROOT_DIR|g" -e "s|listen 8081;|listen $PORT;|g" nginx.conf > "$CONF_RUN"

if [ "$ACTION" = "stop" ]; then
  if [ -f /tmp/os_streaming_nginx.pid ]; then
    nginx -c "$CONF_RUN" -s stop 2>/dev/null || sudo nginx -c "$CONF_RUN" -s stop 2>/dev/null || true
    rm -f /tmp/os_streaming_nginx.pid
  fi
  echo "[+] Stopped System 1 Nginx server."
  exit 0
fi

if [ -f /tmp/os_streaming_nginx.pid ]; then
  nginx -c "$CONF_RUN" -s stop 2>/dev/null || sudo nginx -c "$CONF_RUN" -s stop 2>/dev/null || true
fi

nginx -c "$CONF_RUN" 2>/dev/null || sudo nginx -c "$CONF_RUN"
echo "=================================================================="
echo " [+] System 1 (Linux Nginx + epoll + sendfile) is UP!"
echo "     Web UI (HLS.js + Direct Play): http://127.0.0.1:$PORT"
echo "     Custom OS (QEMU) for compare : http://127.0.0.1:8080"
echo "=================================================================="
