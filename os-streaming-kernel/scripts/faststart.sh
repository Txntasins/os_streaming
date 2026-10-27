#!/bin/sh
# Move the MP4 'moov' index atom to the front of every .mp4 file in www/
# using lossless stream copy (-c copy, NO re-encoding — finishes in ~1-2 seconds even for 1-hour videos!)
set -e
cd "$(dirname "$0")/.."
for f in www/*.mp4; do
  [ -f "$f" ] || continue
  echo "[*] Optimizing MP4 header (+faststart, no re-encode) for: $f"
  tmp="${f}.fast.mp4"
  ffmpeg -y -loglevel warning -i "$f" -c copy -movflags +faststart "$tmp"
  mv "$tmp" "$f"
done
echo "[+] All MP4 files in www/ now have front-loaded moov headers for instant chunked streaming!"
