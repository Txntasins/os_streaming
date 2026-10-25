#!/usr/bin/env bash
# ==============================================================================
# Cloud Video Sync (Solves GitHub's 100 MB file limit for friends cloning repo):
# Reads www/db.json, checks any entry that has a "cloud_url" (e.g. GitHub Release
# asset up to 2 GB, Cloudflare R2, HuggingFace, Archive.org, or Direct MP4 link),
# downloads missing >100 MB videos into www/, runs +faststart, and updates disk.img!
# ==============================================================================
set -e
cd "$(dirname "$0")/.."
mkdir -p www

python3 - <<'PYEOF'
import json, os, subprocess

db_path = "www/db.json"
if not os.path.isfile(db_path):
    print("[!] www/db.json not found.")
    raise SystemExit(0)

with open(db_path, "r", encoding="utf-8") as f:
    db = json.load(f)

catalog = db.get("catalog", {})
downloaded = 0
for fname, meta in catalog.items():
    cloud_url = meta.get("cloud_url", "").strip()
    target = os.path.join("www", fname)
    if not cloud_url:
        continue
    if os.path.isfile(target) and os.path.getsize(target) > 1024:
        print(f"[✓] {fname} already exists locally ({os.path.getsize(target)/(1024*1024):.2f} MB)")
        continue
    print(f"[*] Downloading {fname} from Cloud URL: {cloud_url} ...")
    res = subprocess.run(["curl", "-L", "--fail", "--progress-bar", "-o", target + ".part", cloud_url])
    if res.returncode == 0 and os.path.isfile(target + ".part"):
        os.replace(target + ".part", target)
        print(f"[+] Downloaded {fname} ({os.path.getsize(target)/(1024*1024):.2f} MB)")
        downloaded += 1
    else:
        if os.path.isfile(target + ".part"):
            os.remove(target + ".part")
        print(f"[!] Could not download {fname} locally; Web UI will stream directly from cloud_url.")

if downloaded > 0:
    subprocess.run(["sh", "scripts/faststart.sh"], check=False)
    subprocess.run(["sh", "scripts/mkdisk.sh"], check=False)
PYEOF
