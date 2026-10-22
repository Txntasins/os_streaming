
---

# OS Streaming Kernel (AArch64)

Here's the custom OS we built from scratch for our Operating System class project. It handles everything from booting, memory management, and scheduling, to user mode and disk reading. The coolest part? We integrated a TCP/IP stack (lwIP) and an HTTP server directly into the kernel, so it can stream videos straight to a browser or phone!

It runs entirely on QEMU (using the `virt` board and Cortex-A72). There's no Linux under the hood, and we didn't even use a standard libc (we wrote a minimal one ourselves). The only external code is the lwIP library for networking.

```text
Browser ──HTTP──▶ [QEMU user-net] ──▶ virtio-net ──▶ lwIP ──▶ httpd ──▶ tarfs ──▶ virtio-blk ──▶ disk.img (Video files)
                                                      └─────────────── Our Kernel ───────────────┘

```

## How to Run

If you're on Ubuntu or Arch:

```bash
scripts/setup-arch.sh        # Install the required tools (only need to do this once)
make                         # Compile the code -> build/kernel.elf
make run                     # Create disk.img (if you don't have one) and boot QEMU

```

Once it's running, open your browser and go to **http://localhost:8080**. You'll see our webpage, a sample video, and live kernel stats.
*(To exit QEMU, just press `Ctrl+A` then `X`).*

**Want to stream your own movies?**
Use our script to convert your video into H.264+AAC format and drop it into the `www/` folder:

```bash
scripts/make-video.sh ~/Videos/your_movie.mkv movie.mp4
make disk && make run

```

Then just head to `http://localhost:8080/movie.mp4` to watch it.

## Useful Docs

* `docs/CROSS-PLATFORM.md` — How to run or view the project on Windows/macOS.
* `docs/TEAM-COLLAB.md` — Workflow stuff (GitHub invites, branch protection, CI, and using Docker instead of WSL).
* `docs/PUBLIC-ACCESS.md` — How to share the stream over the internet without messing with router port forwarding (using Cloudflare Tunnel).
* `docs/SECURITY.md` — A quick security audit of our code (what's fixed and what's left to do).

## Shell Commands (Run these inside the QEMU terminal)

| Command | What it does |
| --- | --- |
| `help` | Lists all available commands. |
| `ps` | Shows all running threads and processes with their current states. |
| `mem` | Checks memory usage (page allocator and kernel heap). |
| `ls`, `cat <file>` | Lists files or prints text files from the disk. |
| `ifconfig` | Shows the system IP address (usually `10.0.2.15`). |
| `run [1-8]` | Spawns multiple user programs (EL0) at the same time so you can see address space isolation in action. |
| `crash` | Tests our protection: runs a program that intentionally tries to overwrite kernel memory. The OS catches it and kills just that process without crashing the whole system. |
| `uptime` | Shows how long the system has been running. |

You can also test it from your host terminal:
`curl http://localhost:8080/status` or test a range request with `curl -H "Range: bytes=0-99" http://localhost:8080/sample.mp4`

## Code Structure

| Folder/File | Main Purpose | OS Course Topic |
| --- | --- | --- |
| `arch/boot.S` | CPU entry point, drops privilege to EL1, sets up the stack. | Booting |
| `arch/vectors.S` | Exception vector table. | Interrupts & Exceptions |
| `arch/switch.S` | Context switching. | Process/Thread |
| `arch/mmu.c` | Page tables, identity mapping, and **isolating address spaces per process**. | Virtual Memory & Paging |
| `mm/pmm.c`, `mm/kmalloc.c` | Page allocator (bitmap) and heap. | Memory Management |
| `kernel/thread.c` | Preemptive round-robin scheduler and process management. | CPU Scheduling |
| `kernel/syscall.c`, `kernel/traps.c` | Handles user mode system calls and memory faults. | Protection & User Mode |
| `kernel/shell.c` | UART shell interface. | UI / Syscalls |
| `drivers/` | Simulated hardware drivers (UART, GIC, Timer, Virtio). | Device Drivers & I/O |
| `fs/tarfs.c` | A super simple file system (reads tar files). | File System |
| `net/` | Hooks up lwIP to the network driver and contains the HTTP server (supports Range requests). | Networking |
| `user/` | Sample user program code. | User Space |
| `third_party/lwip` | The TCP/IP Stack library. | – |

## Current Status & Limitations

**What works:**
We've tested this on Ubuntu 24.04 (GCC 13 / QEMU 8.2). It boots fine, handles MMU/Cache/Timers, and can schedule 8 user processes simultaneously. The HTTP server is stable enough to serve large files and handles video seeking (Range/206/404) at around 9 MB/s with no memory leaks.

**Hardware limitations:**
It currently only runs on QEMU because the drivers are strictly written for Virtio. Porting it to a real Raspberry Pi 5 would mean rewriting hardware drivers from scratch (like RP1/Ethernet/SD).

**Other stuff to know:**

* It's strictly single-core for now.
* User programs use flat binaries (we haven't implemented an ELF loader yet).
* The IP address is static (`10.0.2.15`); no DHCP setup yet.
* **We haven't tested this on your machine yet.** Let me know if you run into any weird errors on your setup!
