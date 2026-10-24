# OS Streaming Benchmark Report: Linux (Nginx) vs Custom AArch64 OS

| System | Clients | Throughput (MB/s) | Avg Latency (ms) | P95 Latency (ms) | Success / Total | Dropped |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Linux_Nginx** | 1 | 65.48 MB/s | 2.16 ms | 0.27 ms | 5/5 | 0 |
| **Custom_OS** | 1 | 0.0 MB/s | 0.0 ms | 0.0 ms | 0/5 | 5 |
| **Linux_Nginx** | 2 | 444.09 MB/s | 0.55 ms | 0.81 ms | 10/10 | 0 |
| **Custom_OS** | 2 | 0.0 MB/s | 0.0 ms | 0.0 ms | 0/10 | 10 |
| **Linux_Nginx** | 4 | 538.61 MB/s | 0.96 ms | 1.37 ms | 20/20 | 0 |
| **Custom_OS** | 4 | 0.0 MB/s | 0.0 ms | 0.0 ms | 0/20 | 20 |
| **Linux_Nginx** | 8 | 579.43 MB/s | 1.68 ms | 2.39 ms | 40/40 | 0 |
| **Custom_OS** | 8 | 0.0 MB/s | 0.0 ms | 0.0 ms | 0/40 | 40 |
| **Linux_Nginx** | 16 | 534.85 MB/s | 3.22 ms | 5.35 ms | 80/80 | 0 |
| **Custom_OS** | 16 | 0.0 MB/s | 0.0 ms | 0.0 ms | 0/80 | 80 |
