#!/usr/bin/env bash
# ==============================================================================
# Prepare Multi-Resolution H.264 + AAC HLS Streams (360p & 720p) + Direct Play MP4
# Matches "ระบบที่ 1: Linux + Pi 5 -> Storage: H.264 หลายความละเอียดด้วย FFmpeg"
# ==============================================================================
set -e
cd "$(dirname "$0")/.."
mkdir -p media/hls_360p media/hls_720p

SRC="${1:-../www/TEST.mp4}"
if [ ! -f "$SRC" ]; then
  SRC="../www/sample.mp4"
fi
if [ ! -f "$SRC" ]; then
  echo "[!] No source video found at $SRC"
  exit 1
fi

echo "[*] Copying direct-play MP4 files from ../www/ to ./media/ ..."
for f in ../www/*.mp4; do
  [ -f "$f" ] && cp -f "$f" media/
done

echo "[*] Generating 360p H.264 HLS segments from: $SRC"
ffmpeg -y -loglevel warning -i "$SRC" \
  -vf "scale=-2:360" -c:v libx264 -preset veryfast -crf 24 -pix_fmt yuv420p \
  -g 48 -keyint_min 48 -sc_threshold 0 \
  -c:a aac -b:a 96k \
  -hls_time 4 -hls_playlist_type vod \
  -hls_segment_filename "media/hls_360p/seg_%03d.ts" \
  "media/hls_360p/index.m3u8"

echo "[*] Generating 720p H.264 HLS segments from: $SRC"
ffmpeg -y -loglevel warning -i "$SRC" \
  -vf "scale=-2:720" -c:v libx264 -preset veryfast -crf 22 -pix_fmt yuv420p \
  -g 48 -keyint_min 48 -sc_threshold 0 \
  -c:a aac -b:a 128k \
  -hls_time 4 -hls_playlist_type vod \
  -hls_segment_filename "media/hls_720p/seg_%03d.ts" \
  "media/hls_720p/index.m3u8"

cat > media/master.m3u8 <<'EOF'
#EXTM3U
#EXT-X-VERSION:3
#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360,NAME="360p"
hls_360p/index.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=2400000,RESOLUTION=1280x720,NAME="720p"
hls_720p/index.m3u8
EOF

python3 scripts/sync_catalog.py
echo "[+] Multi-resolution HLS (360p + 720p + master.m3u8) ready in linux-pi5/media/"
