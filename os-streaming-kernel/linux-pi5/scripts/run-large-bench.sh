#!/usr/bin/env bash
# ==============================================================================
# Large-File & Random-Offset Benchmark Runner:
# Finds the largest .mp4 in ../www/ (e.g. TEST1HR.mp4 341MB or TEST.mp4 84MB;
# or creates a 64MB big_test.mp4 if only sample.mp4 exists), syncs it to Nginx,
# and benchmarks random 1 MB Range requests with cold cache!
# ==============================================================================
set -e
cd "$(dirname "$0")/.."
PORT_OS="${1:-8080}"
PORT_LINUX="${2:-8081}"

# Pick the largest .mp4 in ../www/
LARGEST_MP4=$(ls -S ../www/*.mp4 2>/dev/null | head -n 1 || true)
if [ -z "$LARGEST_MP4" ]; then
  echo "[!] No .mp4 files found in ../www/"
  exit 1
fi

FNAME=$(basename "$LARGEST_MP4")
FSIZE=$(stat -c%s "$LARGEST_MP4" 2>/dev/null || echo 0)

# If largest file is under 10 MB (e.g. only sample.mp4 is present), create a 64 MB big_test.mp4
if [ "$FSIZE" -lt 10485760 ]; then
  echo "[*] Largest video ($FNAME) is < 10 MB. Creating 64 MB ../www/big_test.mp4 for large-file cache test..."
  python3 -c '
with open("../www/sample.mp4", "rb") as f: data = f.read()
reps = (64 * 1024 * 1024) // len(data) + 1
with open("../www/big_test.mp4", "wb") as out:
    for _ in range(reps): out.write(data)
'
  FNAME="big_test.mp4"
  (cd .. && sh scripts/mkdisk.sh)
fi

mkdir -p media
cp -f "../www/$FNAME" "media/$FNAME"
sh scripts/run-nginx.sh start "$PORT_LINUX"

echo "[*] Running Large-File Random-Seek Benchmark on: $FNAME"
python3 scripts/benchmark_compare.py \
  --os-url "http://127.0.0.1:$PORT_OS/$FNAME" \
  --linux-url "http://127.0.0.1:$PORT_LINUX/media/$FNAME" \
  --chunk-kb 1024 \
  --random-offset \
  --cold-cache \
  --prefix "benchmark_large"
