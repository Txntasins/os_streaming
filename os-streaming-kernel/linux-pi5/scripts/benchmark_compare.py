#!/usr/bin/env python3
import argparse
import csv
import os
import statistics
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed

def fetch_chunk(url, range_bytes=262144, timeout=10.0):
    req = urllib.request.Request(url, headers={"Range": f"bytes=0-{range_bytes - 1}"})
    t0 = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = resp.read()
            dt = (time.perf_counter() - t0) * 1000.0
            return True, len(data), dt
    except Exception:
        dt = (time.perf_counter() - t0) * 1000.0
        return False, 0, dt

def run_level(name, url, clients, requests_per_client=5, range_bytes=262144):
    total_reqs = clients * requests_per_client
    latencies = []
    total_bytes = 0
    ok_count = 0

    t_start = time.perf_counter()
    with ThreadPoolExecutor(max_workers=clients) as pool:
        futures = [pool.submit(fetch_chunk, url, range_bytes) for _ in range(total_reqs)]
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
    args = parser.parse_args()

    os.makedirs("results", exist_ok=True)
    client_levels = [int(x) for x in args.clients.split(",")]
    rows = []

    print("==========================================================================")
    print(" Benchmarking System 1 (Linux Nginx) vs System 2 (Custom AArch64 OS)")
    print("==========================================================================")

    for c in client_levels:
        for sys_name, url in [("Linux_Nginx", args.linux_url), ("Custom_OS", args.os_url)]:
            res = run_level(sys_name, url, c)
            rows.append(res)
            print(
                f" [{sys_name:<11}] Clients={c:<2} | "
                f"Throughput={res['throughput_mb_s']:>6.2f} MB/s | "
                f"AvgLat={res['avg_latency_ms']:>7.2f} ms | "
                f"P95={res['p95_latency_ms']:>7.2f} ms | "
                f"OK={res['ok_reqs']}/{res['total_reqs']}"
            )

    csv_path = "results/benchmark_results.csv"
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    md_path = "results/benchmark_report.md"
    with open(md_path, "w", encoding="utf-8") as f:
        f.write("# OS Streaming Benchmark Report: Linux (Nginx) vs Custom AArch64 OS\n\n")
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
        ax1.plot(xs, [r["throughput_mb_s"] for r in os_rows], marker="s", linewidth=2, label="System 2: Custom AArch64 OS (lwIP/RR)")
        ax1.set_title("HTTP Streaming Throughput vs Concurrent Clients")
        ax1.set_xlabel("Concurrent Clients")
        ax1.set_ylabel("Throughput (MB/s)")
        ax1.grid(True, alpha=0.3)
        ax1.legend()

        ax2.plot(xs, [r["avg_latency_ms"] for r in linux_rows], marker="o", linewidth=2, label="System 1: Linux + Nginx (epoll/CFS)")
        ax2.plot(xs, [r["avg_latency_ms"] for r in os_rows], marker="s", linewidth=2, label="System 2: Custom AArch64 OS (lwIP/RR)")
        ax2.set_title("Average Chunk Latency vs Concurrent Clients")
        ax2.set_xlabel("Concurrent Clients")
        ax2.set_ylabel("Average Latency (ms)")
        ax2.grid(True, alpha=0.3)
        ax2.legend()

        plt.tight_layout()
        plt.savefig("results/os_vs_linux_benchmark.png", dpi=160)
        print("[+] Saved comparison chart to: results/os_vs_linux_benchmark.png")
    except Exception as e:
        print("[!] Note: install python3-matplotlib to render PNG chart:", e)

    print(f"[+] Saved CSV & Markdown table to: {csv_path} and {md_path}")

if __name__ == "__main__":
    main()
