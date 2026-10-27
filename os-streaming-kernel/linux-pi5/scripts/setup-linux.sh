#!/usr/bin/env bash
set -e
echo "[*] Checking required packages for System 1 (Linux + Pi 5): nginx, ffmpeg, python3-matplotlib..."
if command -v apt-get >/dev/null 2>&1; then
  sudo apt-get update
  sudo apt-get install -y nginx ffmpeg curl python3 python3-matplotlib
elif command -v pacman >/dev/null 2>&1; then
  sudo pacman -Sy --needed --noconfirm nginx ffmpeg curl python python-matplotlib
fi
echo "[+] System 1 dependencies installed."
