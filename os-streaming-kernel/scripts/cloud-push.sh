#!/usr/bin/env bash
# ==============================================================================
# Automatic Cloud Video Uploader (make cloud-push)
# Uploads large videos in www/ to your own GitHub Release ("cloud-videos" tag)
# on https://github.com/Txntasins/os_streaming (supports up to 2 GB per video!),
# automatically fills "cloud_url" in www/db.json, and adds >95MB files to .gitignore!
# ==============================================================================
set -e
cd "$(dirname "$0")/.."

REPO="${GITHUB_REPO:-Txntasins/os_streaming}"
TAG="cloud-videos"

if ! command -v gh >/dev/null 2>&1; then
  echo "[*] Installing GitHub CLI (gh)..."
  if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update && sudo apt-get install -y gh
  elif command -v pacman >/dev/null 2>&1; then
    sudo pacman -Sy --needed --noconfirm github-cli
  else
    echo "[!] Please install 'gh' (GitHub CLI) first: https://cli.github.com/"
    exit 1
  fi
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "[*] Please log in to your GitHub account once to enable Cloud Video Upload:"
  gh auth login
fi

# Ensure the 'cloud-videos' release exists on the repo
if ! gh release view "$TAG" --repo "$REPO" >/dev/null 2>&1; then
  echo "[*] Creating personal Cloud Video storage release '$TAG' on $REPO..."
  gh release create "$TAG" --repo "$REPO" --title "Cloud Video Storage (>100MB)" --notes "Automated cloud storage for OS Streaming Kernel large video files."
fi

# Upload all .mp4 files in www/ larger than 50 MB (or all .mp4 except sample.mp4)
for f in www/*.mp4; do
  [ -f "$f" ] || continue
  fname=$(basename "$f")
  [ "$fname" = "sample.mp4" ] && continue

  size_bytes=$(stat -c%s "$f" 2>/dev/null || echo 0)
  size_mb=$(( size_bytes / 1048576 ))

  echo "[*] Uploading $fname (${size_mb} MB) to your Cloud ($REPO release '$TAG')..."
  gh release upload "$TAG" "$f" --repo "$REPO" --clobber

  cloud_url="https://github.com/$REPO/releases/download/$TAG/$fname"
  echo "[+] Uploaded! Cloud URL: $cloud_url"

  # Auto-update cloud_url in www/db.json
  python3 - "$fname" "$cloud_url" <<'PYUPDATE'
import json, os, sys
fname, url = sys.argv[1], sys.argv[2]
db_path = "www/db.json"
if os.path.isfile(db_path):
    with open(db_path, "r", encoding="utf-8") as fp:
        db = json.load(fp)
    if "catalog" in db and fname in db["catalog"]:
        db["catalog"][fname]["cloud_url"] = url
        with open(db_path, "w", encoding="utf-8") as fp:
            json.dump(db, fp, indent=2, ensure_ascii=False)
            fp.write("\n")
PYUPDATE

  # If file is >= 95 MB, auto-add to .gitignore so 'git push' never fails on GitHub's 100 MB limit!
  if [ "$size_mb" -ge 95 ]; then
    touch ../.gitignore
    if ! grep -q "www/$fname" ../.gitignore 2>/dev/null; then
      echo "os-streaming-kernel/www/$fname" >> ../.gitignore
      echo "www/$fname" >> .gitignore
      git rm --cached "www/$fname" 2>/dev/null || true
      echo "[+] Added www/$fname (${size_mb} MB) to .gitignore (stored on Cloud instead of Git history)"
    fi
  fi
done

sh scripts/mkdisk.sh
echo "=================================================================="
echo " [+] All videos synced to your personal Cloud ($REPO) & www/db.json!"
echo "=================================================================="
