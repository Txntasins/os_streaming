#!/usr/bin/env python3
"""
Head-to-Head Benchmark (Small Cached File vs Large Random-Seek Video):
  System 1: Linux + Nginx (epoll, sendfile, Page Cache, CFS/EEVDF Scheduler)
  System 2: Custom AArch64 OS (virtio-net, lwIP, tarfs, Preemptive MLFQ / RR Scheduler)

Supports:
  - Small file mode (sample.mp4, 154 KB) -> tests warm cache & scheduler concurrency
  - Large file mode (--large, e.g. TEST.mp4 84 MB or TEST1HR.mp4 341 MB or auto-generated 64 MB file)
    with randomized 1 MB byte-range offsets + optional Linux Page Cache drop (--cold-cache)
    to see if the performance gap narrows when Linux Page Cache helps Nginx less!
"""
import argparse
import csv
import os
import random
import statistics
import subprocess
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed

def get_remote_size(url, timeout=5.0):
    req = urllib.request.Request(url, headers={"Range": "bytes=0-15"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            cr = resp.headers.get("Content-Range", "")
            if "/" in cr:
                return int(cr.split("/")[-1])
            cl = resp.headers.get("Content-Length", "0")
            return int(cl)
    except Exception:
        return 0

def fetch_chunk(url, file_size=0, chunk_bytes=262144, random_offset=False, timeout=15.0):
    start = 0
    if random_offset and file_size > chunk_bytes * 2:
        # Align random offset to 4096-byte page boundary across the whole large video
        max_page = (file_size - chunk_bytes) // 4096
        start = random.randint(0, max_page) * 4096
    end = start + chunk_bytes - 1
    req = urllib.request.Request(url, headers={"Range": f"bytes={start}-{end}"})
    t0 = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = resp.read()
            dt = (time.perf_counter() - t0) * 1000.0
            return True, len(data), dt
    except Exception:
        dt = (time.perf_counter() - t0) * 1000.0
        return False, 0, dt

def drop_linux_page_cache():
    try:
        subprocess.run(["sudo", "-n", "sh", "-c", "sync && echo 3 > /proc/sys/vm/drop_caches"],
                       capture_output=True, timeout=3)
    except Exception:
        pass

def run_level(name, url, clients, requests_per_client=5, chunk_bytes=262144, file_size=0, random_offset=False, cold_cache=False):
    if cold_cache and name == "Linux_Nginx":
        drop_linux_page_cache()

    total_reqs = clients * requests_per_client
    latencies = []
    total_bytes = 0
    ok_count = 0

    t_start = time.perf_counter()
    with ThreadPoolExecutor(max_workers=clients) as pool:
        futures = [
            pool.submit(fetch_chunk, url, file_size, chunk_bytes, random_offset)
            for _ in range(total_reqs)
        ]
        for fut in as_completed(futures):
            ok, nbytes, lat_ms = fut.result()
            if ok:
                ok_count += 1
                total_bytes += nbytes
                latencies.append(lat_ms)
    elapsed_s = max(0.001, time.perf_counter() - t_start)

    throughput_mbps = (total_bytes / (1024.0 * 1024.0)) / elapsed_s
    avg_lat = statistics.mean(latencies) if latencies else 0.0
    sorted_lat = sorted(latencies)
    p95_lat = sorted_lat[int(0.95 * (len(sorted_lat) - 1))] if sorted_lat else 0.0
    dropped = total_reqs - ok_count

    return {
        "system": name,
        "clients": clients,
        "total_reqs": total_reqs,
        "ok_reqs": ok_count,
        "dropped": dropped,
        "throughput_mb_s": round(throughput_mbps, 2),
        "avg_latency_ms": round(avg_lat, 2),
        "p95_latency_ms": round(p95_lat, 2),
    }

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--os-url", default="http://127.0.0.1:8080/sample.mp4")
    parser.add_argument("--linux-url", default="http://127.0.0.1:8081/media/sample.mp4")
    parser.add_argument("--clients", default="1,2,4,8,16")
    parser.add_argument("--chunk-kb", type=int, default=256)
    parser.add_argument("--random-offset", action="store_true", help="Randomize Range offsets across large file")
    parser.add_argument("--cold-cache", action="store_true", help="Flush Linux page cache before Linux runs")
    parser.add_argument("--prefix", default="benchmark", help="Output filename prefix in results/")
    args = parser.parse_args()

    os.makedirs("results", exist_ok=True)
    client_levels = [int(x) for x in args.clients.split(",")]
    chunk_bytes = args.chunk_kb * 1024

    os_size = get_remote_size(args.os_url)
    linux_size = get_remote_size(args.linux_url)
    if os_size == 0:
        print(f"[!] ERROR: Custom OS at {args.os_url} is not reachable! Make sure 'make run' is running on port 8080.")
        raise SystemExit(1)
    if linux_size == 0:
        print(f"[!] ERROR: Linux Nginx at {args.linux_url} is not reachable! Make sure 'make server' is running on port 8081.")
        raise SystemExit(1)

    print("==========================================================================")
    print(f" Benchmarking System 1 (Linux Nginx) vs System 2 (Custom AArch64 OS)")
    print(f" Target File Size : {os_size / (1024*1024):.2f} MB | Chunk: {args.chunk_kb} KB | Random Seek: {args.random_offset}")
    print("==========================================================================")

    rows = []
    for c in client_levels:
        for sys_name, url, fsize in [
            ("Linux_Nginx", args.linux_url, linux_size),
            ("Custom_OS", args.os_url, os_size),
        ]:
            res = run_level(
                sys_name, url, c,
                requests_per_client=5,
                chunk_bytes=chunk_bytes,
                file_size=fsize,
                random_offset=args.random_offset,
                cold_cache=args.cold_cache,
            )
            rows.append(res)
            print(
                f" [{sys_name:<11}] Clients={c:<2} | "
                f"Throughput={res['throughput_mb_s']:>6.2f} MB/s | "
                f"AvgLat={res['avg_latency_ms']:>7.2f} ms | "
                f"P95={res['p95_latency_ms']:>7.2f} ms | "
                f"OK={res['ok_reqs']}/{res['total_reqs']}"
            )

    csv_path = f"results/{args.prefix}_results.csv"
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    md_path = f"results/{args.prefix}_report.md"
    with open(md_path, "w", encoding="utf-8") as f:
        f.write(f"# OS Streaming Benchmark ({args.prefix}): Linux (Nginx) vs Custom AArch64 OS\n\n")
        f.write(f"- **File Size**: {os_size / (1024*1024):.2f} MB (`{args.os_url}`)\n")
        f.write(f"- **Chunk Size**: {args.chunk_kb} KB per HTTP 206 Range request (Random Seek: `{args.random_offset}`, Cold Cache: `{args.cold_cache}`)\n\n")
        f.write("| System | Clients | Throughput (MB/s) | Avg Latency (ms) | P95 Latency (ms) | Success / Total | Dropped |\n")
        f.write("| :--- | :---: | :---: | :---: | :---: | :---: | :---: |\n")
        for r in rows:
            f.write(
                f"| **{r['system']}** | {r['clients']} | {r['throughput_mb_s']} MB/s | "
                f"{r['avg_latency_ms']} ms | {r['p95_latency_ms']} ms | "
                f"{r['ok_reqs']}/{r['total_reqs']} | {r['dropped']} |\n"
            )

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        linux_rows = [r for r in rows if r["system"] == "Linux_Nginx"]
        os_rows = [r for r in rows if r["system"] == "Custom_OS"]
        xs = [r["clients"] for r in linux_rows]

        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.5))
        ax1.plot(xs, [r["throughput_mb_s"] for r in linux_rows], marker="o", linewidth=2, label="System 1: Linux + Nginx (epoll/CFS)")
        ax1.plot(xs, [r["throughput_mb_s"] for r in os_rows], marker="s", linewidth=2, label="System 2: Custom AArch64 OS (lwIP/MLFQ)")
        ax1.set_title(f"Throughput vs Clients ({os_size/(1024*1024):.1f} MB file, {args.chunk_kb} KB chunks)")
        ax1.set_xlabel("Concurrent Clients")
        ax1.set_ylabel("Throughput (MB/s)")
        ax1.grid(True, alpha=0.3)
        ax1.legend()

        ax2.plot(xs, [r["avg_latency_ms"] for r in linux_rows], marker="o", linewidth=2, label="System 1: Linux + Nginx (epoll/CFS)")
        ax2.plot(xs, [r["avg_latency_ms"] for r in os_rows], marker="s", linewidth=2, label="System 2: Custom AArch64 OS (lwIP/MLFQ)")
        ax2.set_title(f"Average Latency vs Clients ({os_size/(1024*1024):.1f} MB file)")
        ax2.set_xlabel("Concurrent Clients")
        ax2.set_ylabel("Average Latency (ms)")
        ax2.grid(True, alpha=0.3)
        ax2.legend()

        plt.tight_layout()
        png_path = f"results/{args.prefix}_chart.png"
        plt.savefig(png_path, dpi=160)
        print(f"[+] Saved comparison chart to: {png_path}")
    except Exception as e:
        print("[!] Note: install python3-matplotlib to render PNG chart:", e)

    print(f"[+] Saved CSV & Markdown table to: {csv_path} and {md_path}")

if __name__ == "__main__":
    main()
