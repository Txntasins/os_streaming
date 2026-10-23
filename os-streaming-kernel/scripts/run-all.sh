#!/usr/bin/env bash
# ==============================================================================
# Supervisor script for OS Streaming Kernel:
# 1. Watches www/ every 2s in the background: if you add or delete a video in www/,
#    it automatically updates www/db.json and disk.img in-place without restarting!
# 2. Manages Cloudflare Tunnel ( restarts it automatically when 'os> restart' is typed
#    or when './restart.sh' / 'make restart' is run ).
# ==============================================================================
set -e
cd "$(dirname "$0")/.."

QEMU_BIN="${1:-qemu-system-aarch64}"
KERNEL_ELF="${2:-build/kernel.elf}"
WITH_TUNNEL="${WITH_TUNNEL:-0}"

SERIAL_LOG=".serial.log"
TUNNEL_LOG=".tunnel.log"
TUNNEL_PID_FILE=".tunnel.pid"
WATCHER_PID=""

start_tunnel() {
  if ! command -v cloudflared >/dev/null 2>&1; then
    return 0
  fi
  if [ -f "$TUNNEL_PID_FILE" ]; then
    old_pid=$(cat "$TUNNEL_PID_FILE" 2>/dev/null || true)
    if [ -n "$old_pid" ] && kill -0 "$old_pid" 2>/dev/null; then
      kill "$old_pid" 2>/dev/null || true
      sleep 0.5
    fi
  fi
  : > "$TUNNEL_LOG"
  cloudflared tunnel --url http://127.0.0.1:8080 --protocol http2 --edge-ip-version 4 >"$TUNNEL_LOG" 2>&1 &
  tpid=$!
  echo "$tpid" > "$TUNNEL_PID_FILE"
  # Wait up to 6 seconds to display the trycloudflare.com URL
  for _ in 1 2 3 4 5 6 7 8 9 10 11 12; do
    url=$(grep -oE 'https://[a-zA-Z0-9-]+\.trycloudflare\.com' "$TUNNEL_LOG" 2>/dev/null | head -n 1 || true)
    if [ -n "$url" ]; then
      printf "\r\n======================================================================\r\n"
      printf " [Cloudflare Tunnel Ready] %s\r\n" "$url"
      printf "======================================================================\r\n"
      break
    fi
    sleep 0.5
  done
}

# Background watcher: detects added/deleted videos in www/ AND detects 'os> restart'
watch_loop() {
  last_sig=$(ls -l --time-style=+%s www 2>/dev/null | grep -v 'db\.json' | sha256sum | cut -d' ' -f1)
  while true; do
    sleep 2
    # 1. Check if any file in www/ (excluding db.json) was added, deleted, or resized
    cur_sig=$(ls -l --time-style=+%s www 2>/dev/null | grep -v 'db\.json' | sha256sum | cut -d' ' -f1)
    if [ "$cur_sig" != "$last_sig" ]; then
      sh scripts/mkdisk.sh --quiet >/dev/null 2>&1 || true
      last_sig=$(ls -l --time-style=+%s www 2>/dev/null | grep -v 'db\.json' | sha256sum | cut -d' ' -f1)
    fi

    # 2. Check if 'os> restart' (or /api/restart) printed [HOST_RESTART_REQUESTED]
    if [ -f "$SERIAL_LOG" ] && grep -q "\[HOST_RESTART_REQUESTED\]" "$SERIAL_LOG" 2>/dev/null; then
      : > "$SERIAL_LOG"
      sh scripts/mkdisk.sh --quiet >/dev/null 2>&1 || true
      last_sig=$(ls -l --time-style=+%s www 2>/dev/null | grep -v 'db\.json' | sha256sum | cut -d' ' -f1)
      # If cloudflared tunnel is running (even in another terminal) or WITH_TUNNEL=1, restart it!
      if pgrep -x cloudflared >/dev/null 2>&1 || [ "$WITH_TUNNEL" = "1" ]; then
        pkill -x cloudflared 2>/dev/null || true
        start_tunnel
      fi
    fi
  done
}

cleanup() {
  if [ -n "$WATCHER_PID" ]; then
    kill "$WATCHER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

: > "$SERIAL_LOG"
sh scripts/mkdisk.sh

if [ "$WITH_TUNNEL" = "1" ]; then
  start_tunnel
fi

watch_loop &
WATCHER_PID=$!

# Run QEMU with writethrough disk cache (so in-place disk.img updates are seen immediately)
# and tee serial output to .serial.log while keeping interactive terminal 100% functional
exec "$QEMU_BIN" \
  -M virt,gic-version=2 -cpu cortex-a72 -m 256M -nographic \
  -drive file=disk.img,if=none,format=raw,cache=writethrough,id=hd0 \
  -device virtio-blk-device,drive=hd0 \
  -netdev user,id=net0,hostfwd=tcp::8080-:80 \
  -device virtio-net-device,netdev=net0 \
  -chardev stdio,id=con0,mux=on,logfile="$SERIAL_LOG",signal=off \
  -mon chardev=con0,mode=readline \
  -serial chardev:con0 \
  -kernel "$KERNEL_ELF"
