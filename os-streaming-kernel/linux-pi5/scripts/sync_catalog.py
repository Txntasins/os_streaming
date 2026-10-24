#!/usr/bin/env python3
import os
import json

os.makedirs("media", exist_ok=True)
mp4s = sorted([f for f in os.listdir("media") if f.endswith(".mp4")])
data = {
    "system": "System 1: Linux + Raspberry Pi 5 (Nginx epoll + HLS + Direct Play)",
    "hls_master": "/media/master.m3u8" if os.path.isfile("media/master.m3u8") else "",
    "hls_360p": "/media/hls_360p/index.m3u8" if os.path.isfile("media/hls_360p/index.m3u8") else "",
    "hls_720p": "/media/hls_720p/index.m3u8" if os.path.isfile("media/hls_720p/index.m3u8") else "",
    "mp4_files": [
        {"filename": f, "url": f"/media/{f}", "size": os.path.getsize(os.path.join("media", f))}
        for f in mp4s
    ],
}
with open("media/catalog.json", "w", encoding="utf-8") as f:
    json.dump(data, f, indent=2)
