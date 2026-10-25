# OS Streaming Benchmark Report: Linux (Nginx) vs Custom AArch64 OS

| System | Clients | Throughput (MB/s) | Avg Latency (ms) | P95 Latency (ms) | Success / Total | Dropped |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Linux_Nginx** | 1 | 95.23 MB/s | 1.44 ms | 0.32 ms | 5/5 | 0 |
| **Custom_OS** | 1 | 23.42 MB/s | 6.33 ms | 4.77 ms | 5/5 | 0 |
| **Linux_Nginx** | 2 | 396.35 MB/s | 0.64 ms | 0.8 ms | 10/10 | 0 |
| **Custom_OS** | 2 | 33.25 MB/s | 8.66 ms | 8.91 ms | 10/10 | 0 |
| **Linux_Nginx** | 4 | 550.46 MB/s | 0.94 ms | 1.27 ms | 20/20 | 0 |
| **Custom_OS** | 4 | 31.84 MB/s | 18.11 ms | 21.12 ms | 20/20 | 0 |
| **Linux_Nginx** | 8 | 591.21 MB/s | 1.64 ms | 2.43 ms | 40/40 | 0 |
| **Custom_OS** | 8 | 32.51 MB/s | 35.06 ms | 37.23 ms | 40/40 | 0 |
| **Linux_Nginx** | 16 | 594.07 MB/s | 2.74 ms | 4.83 ms | 80/80 | 0 |
| **Custom_OS** | 16 | 25.95 MB/s | 88.13 ms | 117.88 ms | 80/80 | 0 |
