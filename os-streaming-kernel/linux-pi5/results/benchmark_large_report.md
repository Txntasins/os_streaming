# OS Streaming Benchmark (benchmark_large): Linux (Nginx) vs Custom AArch64 OS

- **File Size**: 343.60 MB (`http://127.0.0.1:8080/TEST1HR.mp4`)
- **Chunk Size**: 1024 KB per HTTP 206 Range request (Random Seek: `True`, Cold Cache: `True`)

| System | Clients | Throughput (MB/s) | Avg Latency (ms) | P95 Latency (ms) | Success / Total | Dropped |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Linux_Nginx** | 1 | 770.93 MB/s | 1.16 ms | 1.2 ms | 5/5 | 0 |
| **Custom_OS** | 1 | 27.72 MB/s | 35.96 ms | 35.06 ms | 5/5 | 0 |
| **Linux_Nginx** | 2 | 1577.36 MB/s | 1.1 ms | 1.46 ms | 10/10 | 0 |
| **Custom_OS** | 2 | 28.02 MB/s | 71.09 ms | 76.24 ms | 10/10 | 0 |
| **Linux_Nginx** | 4 | 1981.45 MB/s | 1.73 ms | 2.91 ms | 20/20 | 0 |
| **Custom_OS** | 4 | 24.7 MB/s | 161.45 ms | 170.39 ms | 20/20 | 0 |
| **Linux_Nginx** | 8 | 2165.0 MB/s | 3.26 ms | 4.75 ms | 40/40 | 0 |
| **Custom_OS** | 8 | 24.35 MB/s | 313.5 ms | 338.19 ms | 40/40 | 0 |
| **Linux_Nginx** | 16 | 2098.24 MB/s | 6.61 ms | 10.11 ms | 80/80 | 0 |
| **Custom_OS** | 16 | 20.5 MB/s | 779.02 ms | 792.87 ms | 80/80 | 0 |
