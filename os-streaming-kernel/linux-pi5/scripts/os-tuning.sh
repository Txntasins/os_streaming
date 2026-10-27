#!/usr/bin/env bash
# ==============================================================================
# Linux OS Tuning Script for Raspberry Pi 5 / Linux (System 1):
# 1. Scheduler tuning: nice (-10) & CFS scheduler metrics (/proc/<pid>/sched)
# 2. cgroups v2: CPU weight/quota & memory ceiling (/sys/fs/cgroup/os_streaming)
# 3. Memory: ZRAM status & Page Cache inspection / Cold-vs-Warm cache test
# ==============================================================================
set -e

MODE="${1:-status-and-tune}"
CG_DIR="/sys/fs/cgroup/os_streaming"

echo "=================================================================="
echo " System 1: Linux OS Tuning & Kernel Telemetry (Scheduler/MM/I/O)"
echo "=================================================================="

PIDS=$(pgrep -f "nginx: worker process" || true)
if [ -z "$PIDS" ]; then
  echo "[!] Nginx is not running yet. Start it first with: make server"
else
  echo "[1] Scheduler (Linux CFS/EEVDF + nice priority):"
  for pid in $PIDS; do
    renice -n -10 -p "$pid" >/dev/null 2>&1 || true
    echo "    -> Tuned Nginx worker PID $pid to nice -10 (high CPU priority)"
    if [ -f "/proc/$pid/sched" ]; then
      grep -E "se\.exec_start|se\.sum_exec_runtime|nr_switches" "/proc/$pid/sched" | sed 's/^/       /' || true
    fi
  done
fi

echo ""
echo "[2] cgroups v2 Resource Control ($CG_DIR):"
if [ -d /sys/fs/cgroup ]; then
  mkdir -p "$CG_DIR" 2>/dev/null || true
  if [ -w "$CG_DIR" ]; then
    echo "200" > "$CG_DIR/cpu.weight" 2>/dev/null || true
    echo "536870912" > "$CG_DIR/memory.max" 2>/dev/null || true
    for pid in $PIDS; do
      echo "$pid" > "$CG_DIR/cgroup.procs" 2>/dev/null || true
    done
    echo "    -> Attached Nginx workers to $CG_DIR (cpu.weight=200, memory.max=512MB)"
    [ -f "$CG_DIR/memory.current" ] && echo "    -> Current cgroup memory usage: $(cat "$CG_DIR/memory.current") bytes"
  else
    echo "    -> Run with sudo to create/modify $CG_DIR"
  fi
fi

echo ""
echo "[3] Memory Subsystem (ZRAM & Page Cache):"
if command -v zramctl >/dev/null 2>&1; then
  zramctl || true
fi
grep -E "MemTotal|MemFree|Cached|Buffers|SwapTotal" /proc/meminfo | sed 's/^/    /'

if [ "$MODE" = "drop-cache" ]; then
  echo "[*] Flushing Linux Page Cache (vm.drop_caches=3) for Cold-Cache test..."
  sync
  echo 3 > /proc/sys/vm/drop_caches
  echo "[+] Page Cache flushed."
fi
echo "=================================================================="
