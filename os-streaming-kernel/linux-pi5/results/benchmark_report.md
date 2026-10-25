# OS Streaming Benchmark (benchmark): Linux (Nginx) vs Custom AArch64 OS

- **File Size**: 0.15 MB (`http://127.0.0.1:8080/sample.mp4`)
- **Chunk Size**: 256 KB per HTTP 206 Range request (Random Seek: `False`, Cold Cache: `False`)

| System | Clients | Throughput (MB/s) | Avg Latency (ms) | P95 Latency (ms) | Success / Total | Dropped |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Linux_Nginx** | 1 | 57.19 MB/s | 2.52 ms | 0.43 ms | 5/5 | 0 |
| **Custom_OS** | 1 | 27.44 MB/s | 5.38 ms | 4.86 ms | 5/5 | 0 |
| **Linux_Nginx** | 2 | 422.17 MB/s | 0.59 ms | 0.65 ms | 10/10 | 0 |
| **Custom_OS** | 2 | 30.72 MB/s | 9.37 ms | 9.84 ms | 10/10 | 0 |
| **Linux_Nginx** | 4 | 546.78 MB/s | 0.93 ms | 1.37 ms | 20/20 | 0 |
| **Custom_OS** | 4 | 25.03 MB/s | 21.42 ms | 38.92 ms | 20/20 | 0 |
| **Linux_Nginx** | 8 | 324.92 MB/s | 3.02 ms | 5.33 ms | 40/40 | 0 |
| **Custom_OS** | 8 | 24.6 MB/s | 46.72 ms | 60.53 ms | 40/40 | 0 |
| **Linux_Nginx** | 16 | 523.73 MB/s | 3.27 ms | 5.56 ms | 80/80 | 0 |
| **Custom_OS** | 16 | 22.46 MB/s | 101.93 ms | 112.85 ms | 80/80 | 0 |
