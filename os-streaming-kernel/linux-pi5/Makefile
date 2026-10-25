.RECIPEPREFIX = >
PORT_LINUX ?= 8081
PORT_OS    ?= 8080
VIDEO      ?= ../www/sample.mp4

all: help

help:
> @echo "=================================================================="
> @echo " System 1: Linux + Raspberry Pi 5 (Nginx + FFmpeg HLS + OS Tuning)"
> @echo "=================================================================="
> @echo "  make setup      - Install nginx, ffmpeg, and benchmark tools"
> @echo "  make hls        - Convert video into multi-res H.264 HLS (360p/720p)"
> @echo "  make server     - Start real Nginx (epoll + sendfile) on port $(PORT_LINUX)"
> @echo "  make stop       - Stop Nginx on port $(PORT_LINUX)"
> @echo "  make tune       - Apply Linux OS tuning (nice, cgroups v2, zram, page cache)"
> @echo "  make benchmark  - Benchmark Small File (sample.mp4 154KB, Warm Cache)
> @echo "  make benchmark  - Benchmark Large Video (TEST.mp4/TEST1HR.mp4, 1MB Random Seek + Cold Cache)""
> @echo "=================================================================="

setup:
> sh scripts/setup-linux.sh

hls:
> sh scripts/prepare-hls.sh "$(VIDEO)"

server:
> sh scripts/run-nginx.sh start "$(PORT_LINUX)"

stop:
> sh scripts/run-nginx.sh stop "$(PORT_LINUX)"

tune:
> sudo sh scripts/os-tuning.sh status-and-tune

benchmark:
> python3 scripts/benchmark_compare.py --os-url "http://127.0.0.1:$(PORT_OS)/sample.mp4" --linux-url "http://127.0.0.1:$(PORT_LINUX)/media/sample.mp4"

benchmark-large:
> sh scripts/run-large-bench.sh "$(PORT_OS)" "$(PORT_LINUX)"

.PHONY: all help setup hls server stop tune benchmark benchmark-large
