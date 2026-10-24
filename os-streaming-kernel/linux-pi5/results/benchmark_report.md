# OS Streaming Benchmark Report: Linux (Nginx) vs Custom AArch64 OS

| System | Clients | Throughput (MB/s) | Avg Latency (ms) | P95 Latency (ms) | Success / Total | Dropped |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Linux_Nginx** | 1 | 0.0 MB/s | 0.0 ms | 0.0 ms | 0/5 | 5 |
| **Custom_OS** | 1 | 23.09 MB/s | 6.41 ms | 6.79 ms | 5/5 | 0 |
| **Linux_Nginx** | 2 | 0.0 MB/s | 0.0 ms | 0.0 ms | 0/10 | 10 |
| **Custom_OS** | 2 | 30.29 MB/s | 9.54 ms | 10.84 ms | 10/10 | 0 |
| **Linux_Nginx** | 4 | 0.0 MB/s | 0.0 ms | 0.0 ms | 0/20 | 20 |
| **Custom_OS** | 4 | 32.75 MB/s | 17.6 ms | 18.58 ms | 20/20 | 0 |
| **Linux_Nginx** | 8 | 0.0 MB/s | 0.0 ms | 0.0 ms | 0/40 | 40 |
| **Custom_OS** | 8 | 29.13 MB/s | 39.63 ms | 48.55 ms | 40/40 | 0 |
| **Linux_Nginx** | 16 | 0.0 MB/s | 0.0 ms | 0.0 ms | 0/80 | 80 |
| **Custom_OS** | 16 | 27.13 MB/s | 84.31 ms | 94.0 ms | 80/80 | 0 |
