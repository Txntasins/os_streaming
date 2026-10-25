#!/bin/sh
# ==============================================================================
# Create/update disk.img = tar (ustar) of www/ directory
# - Automatically syncs www/db.json with actual video files in www/
#   (adds newly uploaded videos automatically & removes deleted videos!)
# - Writes in-place (conv=notrunc,fsync) so running QEMU sees changes immediately
# ==============================================================================
set -e
cd "$(dirname "$0")/.."
[ -d www ] || { echo "Directory www/ not found"; exit 1; }
QUIET="${1:-}"

# Step 1: Auto-sync www/db.json with actual media files in www/
python3 - <<'PY_SYNC'
import os, json, subprocess

www_dir = "www"
db_path = os.path.join(www_dir, "db.json")
media_exts = (".mp4", ".m4v", ".webm", ".mkv", ".mp3", ".m4a", ".aac")

default_db = {
    "platform": "OS StreamFlix (AArch64 Kernel + Pi 5)",
    "version": "2.2",
    "collections": [
        { "id": "all", "name": "All Videos", "icon": "🎬" },
        { "id": "featured", "name": "Featured", "icon": "🔥" },
        { "id": "Video", "name": "Long Videos", "icon": "🎞️" },
        { "id": "music", "name": "Music & Rickroll", "icon": "🎵" },
        { "id": "demo", "name": "Kernel Demos", "icon": "⚡" },
        { "id": "watchlist", "name": "My Watchlist", "icon": "⭐" }
    ],
    "catalog": {}
}

db = default_db
if os.path.isfile(db_path):
    try:
        with open(db_path, "r", encoding="utf-8") as f:
            loaded = json.load(f)
            if isinstance(loaded, dict):
                db["collections"] = loaded.get("collections", default_db["collections"])
                db["catalog"] = loaded.get("catalog", {})
    except Exception:
        pass

# Scan current media files in www/
actual_files = sorted([
    f for f in os.listdir(www_dir)
    if os.path.isfile(os.path.join(www_dir, f)) and f.lower().endswith(media_exts) and not f.endswith(".tmp")
])

# Remove deleted local files from catalog UNLESS they have a "cloud_url" (Cloud Video DB entry!)
old_keys = list(db["catalog"].keys())
for k in old_keys:
    has_cloud = bool(db["catalog"][k].get("cloud_url", "").strip())
    if k not in actual_files and not has_cloud:
        del db["catalog"][k]

palette = ["#e50914", "#0284c7", "#8B5E3C", "#7c3aed", "#059669", "#d97706"]

def probe_media(filepath):
    dur_str = "Stream"
    res_str = "H.264/AAC MP4"
    try:
        cmd = [
            "ffprobe", "-v", "error", "-select_streams", "v:0",
            "-show_entries", "stream=height:format=duration",
            "-of", "json", filepath
        ]
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=3)
        if res.returncode == 0:
            info = json.loads(res.stdout)
            streams = info.get("streams", [])
            if streams and "height" in streams[0]:
                res_str = f"{streams[0]['height']}p H.264/AAC"
            dur = float(info.get("format", {}).get("duration", 0))
            if dur > 0:
                h = int(dur // 3600)
                m = int((dur % 3600) // 60)
                s = int(dur % 60)
                dur_str = f"{h:02d}:{m:02d}:{s:02d}" if h > 0 else f"{m}:{s:02d}"
    except Exception:
        pass
    return dur_str, res_str

# Auto-add any newly added media files
for idx, fname in enumerate(actual_files):
    if fname not in db["catalog"]:
        fpath = os.path.join(www_dir, fname)
        size_mb = os.path.getsize(fpath) / (1024.0 * 1024.0)
        dur_str, res_str = probe_media(fpath)
        base_title = os.path.splitext(fname)[0].replace("_", " ").replace("-", " ")
        db["catalog"][fname] = {
            "title": base_title,
            "collection": "Video" if size_mb > 20 else "demo",
            "featured": True,
            "resolution": res_str,
            "duration": dur_str,
            "year": 2026,
            "badge": "AUTO-INDEXED",
            "accent": palette[idx % len(palette)],
            "description": f"Auto-indexed from www/{fname} ({size_mb:.2f} MB) via tarfs + HTTP 206 chunked streaming."
        }

with open(db_path, "w", encoding="utf-8") as f:
    json.dump(db, f, indent=2, ensure_ascii=False)
    f.write("\n")
PY_SYNC

# Step 2: Build tar archive in a temp file and write in-place to disk.img (preserving inode for QEMU)
DISK_SIZE="${DISK_SIZE:-4G}"
[ -f disk.img ] || truncate -s "$DISK_SIZE" disk.img
tar --format=ustar --owner=0 --group=0 --numeric-owner -cf .disk.img.tmp -C www .
dd if=.disk.img.tmp of=disk.img bs=1M conv=notrunc,fsync status=none
rm -f .disk.img.tmp
truncate -s "$DISK_SIZE" disk.img
sync

if [ "$QUIET" != "--quiet" ]; then
  echo "Synced www/db.json & updated disk.img ($DISK_SIZE total): $(ls www | tr '\n' ' ')"
fi
