# OS Streaming Benchmark (benchmark_large): Linux (Nginx) vs Custom AArch64 OS

- **File Size**: 343.60 MB (`http://127.0.0.1:8080/TEST1HR.mp4`)
- **Chunk Size**: 1024 KB per HTTP 206 Range request (Random Seek: `True`, Cold Cache: `True`)

| System | Clients | Throughput (MB/s) | Avg Latency (ms) | P95 Latency (ms) | Success / Total | Dropped |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Linux_Nginx** | 1 | 1468.91 MB/s | 0.57 ms | 0.81 ms | 5/5 | 0 |
| **Custom_OS** | 1 | 27.62 MB/s | 36.07 ms | 37.1 ms | 5/5 | 0 |
| **Linux_Nginx** | 2 | 1997.18 MB/s | 0.83 ms | 1.37 ms | 10/10 | 0 |
| **Custom_OS** | 2 | 28.6 MB/s | 69.59 ms | 77.9 ms | 10/10 | 0 |
| **Linux_Nginx** | 4 | 2092.65 MB/s | 1.67 ms | 2.27 ms | 20/20 | 0 |
| **Custom_OS** | 4 | 28.29 MB/s | 140.83 ms | 149.65 ms | 20/20 | 0 |
| **Linux_Nginx** | 8 | 2467.7 MB/s | 2.86 ms | 4.37 ms | 40/40 | 0 |
| **Custom_OS** | 8 | 27.75 MB/s | 286.7 ms | 322.79 ms | 40/40 | 0 |
| **Linux_Nginx** | 16 | 2277.08 MB/s | 6.0 ms | 8.47 ms | 80/80 | 0 |
| **Custom_OS** | 16 | 24.67 MB/s | 645.72 ms | 658.01 ms | 80/80 | 0 |
