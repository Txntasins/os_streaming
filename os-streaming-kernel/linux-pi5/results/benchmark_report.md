# OS Streaming Benchmark (benchmark): Linux (Nginx) vs Custom AArch64 OS

- **File Size**: 0.15 MB (`http://127.0.0.1:8080/sample.mp4`)
- **Chunk Size**: 256 KB per HTTP 206 Range request (Random Seek: `False`, Cold Cache: `False`)

| System | Clients | Throughput (MB/s) | Avg Latency (ms) | P95 Latency (ms) | Success / Total | Dropped |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Linux_Nginx** | 1 | 378.15 MB/s | 0.29 ms | 0.27 ms | 5/5 | 0 |
| **Custom_OS** | 1 | 22.1 MB/s | 6.67 ms | 6.79 ms | 5/5 | 0 |
| **Linux_Nginx** | 2 | 410.35 MB/s | 0.61 ms | 0.76 ms | 10/10 | 0 |
| **Custom_OS** | 2 | 20.84 MB/s | 13.81 ms | 14.81 ms | 10/10 | 0 |
| **Linux_Nginx** | 4 | 467.55 MB/s | 1.02 ms | 1.44 ms | 20/20 | 0 |
| **Custom_OS** | 4 | 27.76 MB/s | 20.87 ms | 24.67 ms | 20/20 | 0 |
| **Linux_Nginx** | 8 | 490.42 MB/s | 1.94 ms | 3.29 ms | 40/40 | 0 |
| **Custom_OS** | 8 | 28.95 MB/s | 39.31 ms | 47.51 ms | 40/40 | 0 |
| **Linux_Nginx** | 16 | 493.76 MB/s | 3.74 ms | 6.81 ms | 80/80 | 0 |
| **Custom_OS** | 16 | 25.0 MB/s | 91.26 ms | 109.11 ms | 80/80 | 0 |
