# 🎬 OS NatFlix — Custom AArch64 OS Streaming Kernel & Raspberry Pi 5 Bare-Metal Suite

โปรเจกต์วิชา **Operating Systems**: ระบบสตรีมมิงวิดีโอที่พัฒนาขึ้นแบบขนาน **2 ระบบ** เพื่อเปรียบเทียบสถาปัตยกรรมและสมรรถนะ:
1. **ระบบที่ 1 (Linux + Raspberry Pi 5 Baseline)**: ใช้ **Nginx จริง (`epoll`, `sendfile`, `tcp_nopush`)** ส่งสตรีมวิดีโอหลายความละเอียด (**HLS `.m3u8` / `.ts` `360p` & `720p`** เตรียมด้วย FFmpeg + Direct Play MP4) พร้อมการปรับจูน OS (`nice`, `cgroups v2`, `zram`, `Page Cache`) และชุดวัดผล Benchmark เทียบที่ `1, 2, 4, 8, 16` Clients
2. **ระบบที่ 2 (Custom AArch64 OS Kernel — เขียนเองตั้งแต่ศูนย์โดยไม่พึ่งพา Linux)**:
   - **Kernel Core (AArch64)**: Boot (`boot.S`), MMU & Identity Paging (`mmu.c`), GICv2 Interrupt Controller, 100 Hz Hardware Timer (`cntvct_el0`), Preemptive **Round-Robin** & **4-Level Priority MLFQ Scheduler** (`thread.c`), User Mode (`EL0`) + Syscalls + Memory Protection (`crash` test) และ Interactive Shell (`os>`)
   - **Storage & Network Stack**: `virtio-blk` $\rightarrow$ `tarfs` (`disk.img` ขนาด 4 GB รองรับ Hot-Reload) $\rightarrow$ `virtio-net` $\rightarrow$ `lwIP` TCP/IP $\rightarrow$ Custom HTTP/1.1 Server (`net/httpd.c`) รองรับ **HTTP 206 Partial Content (1 MB Chunked Range Buffering)**, Persistent Keep-Alive, Cloudflare Tunnel และระบบ **Cloud Video Storage (GitHub Releases)** สำหรับไฟล์วิดีโอขนาดใหญ่ (>100 MB)
   - **Raspberry Pi 5 Bare-Metal Hardware Bring-Up**:
     - **RP1 UART0 (`0x1f00030000`)**: Serial Console ความเร็ว 115200 8N1 (`pi5-uart-bringup/`)
     - **BCM2712 VideoCore VII Mailbox0 (`0x107c013880`)**: HDMI Framebuffer 1280x720x32 ARGB (`pi5-framebuffer-bringup/`)
     - **Bare-Metal Audio ครบ 3 สถาปัตยกรรม (`drivers/audio.c` & `pi5-av-bringup/`)**: **RP1 PWM0 Audio** (`0x1f00098000`), **RP1 DesignWare I2S0 + DMA** (`0x1f000a0000` สำหรับ DAC PCM5102A) และ **BCM2712 VideoCore VII `vc4-hdmi` Audio** (`0x107c701400` พร้อมตัวอ่าน EDID CEA-861 SAD, คำนวณ Clock Regeneration `N=6144 / CTS=74250`, สร้าง Audio InfoFrame `0x84` Checksum `0x70` และเข้ารหัส IEC60958 Subframes ส่งเข้า MAI FIFO)
     - **MJPEG Video Decoder + Kernel Timer A/V Sync (`kernel/mjpeg_av.c`)**: ถอดรหัส MJPEG Baseline (8x8 Integer IDCT + BT.601 YCbCr $\rightarrow$ ARGB8888) และซิงก์เวลาแสดงผลกับ Audio Master Clock ด้วย ARM Generic Timer (`cntvct_el0`)

## โครงสร้างโฟลเดอร์ของโปรเจกต์

* `os_streaming/` — โฟลเดอร์นอกสุดของ Git Repository
* `os_streaming/os-streaming-kernel/` — **โฟลเดอร์หลักของ Custom OS (ใช้รันคำสั่ง `make` ส่วนใหญ่ทั้งหมด)**
* `os_streaming/os-streaming-kernel/www/` — โฟลเดอร์เก็บหน้าเว็บ `index.html`, ฐานข้อมูล `db.json` และไฟล์วิดีโอ `.mp4`
* `os_streaming/os-streaming-kernel/linux-pi5/` — โฟลเดอร์ระบบที่ 1 (Linux Nginx + FFmpeg HLS + OS Tuning + Benchmark)
* `os_streaming/os-streaming-kernel/pi5-uart-bringup/` — โฟลเดอร์ทดสอบ Bare-Metal RP1 UART0 บนบอร์ด Raspberry Pi 5
* `os_streaming/os-streaming-kernel/pi5-framebuffer-bringup/` — โฟลเดอร์ทดสอบ Bare-Metal HDMI Framebuffer 1280x720 บนบอร์ด Raspberry Pi 5
* `os_streaming/os-streaming-kernel/pi5-av-bringup/` — โฟลเดอร์รวม Bare-Metal HDMI Framebuffer + Audio (PWM/I2S/vc4-hdmi) + MJPEG A/V Sync สำหรับบอร์ด Raspberry Pi 5

## หมวดที่ 1: เริ่มต้นดึงโปรเจกต์ครั้งแรก (Clone) & คำสั่ง Git

### 1.1 ดึงโปรเจกต์ครั้งแรกสำหรับคนที่ยังไม่เคยมีโปรเจกต์ในเครื่อง

**Path ที่ใช้รัน:** โฟลเดอร์ไหนก็ได้ในเครื่องที่ต้องการเก็บงาน (เช่น `~` หรือ `D:\`)

```bash
git clone https://github.com/Txntasins/os_streaming.git
cd os_streaming/os-streaming-kernel
chmod +x scripts/*.sh linux-pi5/scripts/*.sh linux-pi5/scripts/*.py
make cloud-pull
```

### 1.2 ตารางคำสั่ง Git สำหรับอัปเดตและส่งงาน

**Path ที่ต้อง `cd` เข้าไปก่อน:** `cd ~/os_streaming/os-streaming-kernel`

| คำสั่ง | ทำหน้าที่อะไร |
| :--- | :--- |
| `git pull origin main` | ดึงโค้ดและ `www/db.json` เวอร์ชันล่าสุดจาก GitHub ลงเครื่อง |
| `git status` | ตรวจสอบว่ามีไฟล์ไหนถูกแก้ไข เพิ่ม หรือลบไปบ้าง |
| `git add .` | เตรียมไฟล์ทั้งหมดเข้าสู่ Stage เพื่อรอ Commit |
| `git commit -m "Update - ..."` | บันทึกประวัติการแก้ไขลงเครื่องพร้อมข้อความกำกับ |
| `git push origin main` | ส่งโค้ดที่ Commit แล้วขึ้นไปบน GitHub |
| `git push --force-with-lease origin main` | บังคับส่งโค้ดทับขึ้น GitHub (ใช้เฉพาะกรณีที่เคยสั่ง `git reset` ย้อน Commit ในเครื่อง) |
| `git reset --mixed HEAD~1` | ยกเลิก Commit ล่าสุด 1 ขั้นโดยที่ไฟล์โค้ดในเครื่องไม่หาย (ใช้เวลาเผลอ `git add` ไฟล์ใหญ่เกิน 100 MB ติดเข้าไป) |

## หมวดที่ 2: สำหรับเพื่อนที่ใช้ Windows (เลือกได้ 2 วิธี)

เพื่อนที่ใช้ Windows สามารถรันโปรเจกต์นี้ได้ 2 วิธี คือ **วิธีที่ 2.1 ผ่าน Docker Desktop (ง่ายสุด ไม่ต้องลง Linux)** หรือ **วิธีที่ 2.2 ผ่าน WSL2 (Ubuntu บน Windows — แนะนำถ้าต้องการรันครบทั้ง Benchmark และ Nginx)**

### วิธีที่ 2.1: รันผ่าน Docker Desktop บน Windows (PowerShell หรือ CMD)

**สิ่งที่ต้องติดตั้งก่อน:** ติดตั้ง [Docker Desktop for Windows](https://www.docker.com/products/docker-desktop/) และ [Git for Windows](https://git-scm.com/download/win)
**Path ที่ต้อง `cd` เข้าไปก่อน (ใน PowerShell):** `cd os_streaming\os-streaming-kernel`

```powershell
# 1. ดึงโปรเจกต์จาก GitHub
git clone https://github.com/Txntasins/os_streaming.git
cd os_streaming\os-streaming-kernel

# 2. สร้าง Docker Image (รันครั้งแรกครั้งเดียว มี Cross-compiler และ QEMU ครบในตัว)
docker build -t os-streaming .

# 3. รัน Custom OS Kernel บนพอร์ต 8080
docker run --rm -it -p 8080:8080 os-streaming
```

| คำสั่ง PowerShell (Windows Docker) | ทำหน้าที่อะไร |
| :--- | :--- |
| `docker build -t os-streaming .` | สร้างอิมเมจจำลองที่มี `aarch64-linux-gnu-gcc` และ `qemu-system-aarch64` พร้อมคอมไพล์ Kernel |
| `docker run --rm -it -p 8080:8080 os-streaming` | เปิดรัน Custom OS และเชื่อมพอร์ต `8080` ออกมาที่เบราว์เซอร์ Windows (`http://localhost:8080`) |
| `docker run --rm -it -p 8080:8080 -v "${PWD}/www:/os/www" os-streaming` | รันพร้อมเชื่อมโฟลเดอร์ `www` บน Windows เข้าไปใน Container เพื่อเพิ่มคลิป `.mp4` ได้สดๆ |

### วิธีที่ 2.2: รันผ่าน WSL2 (Ubuntu บน Windows — รันได้ทุกคำสั่งเหมือน Linux 100%)

**ขั้นที่ 1 (เปิด PowerShell แบบ Run as Administrator บน Windows):**

```powershell
wsl --install -d Ubuntu
```

*(รีสตาร์ทเครื่อง 1 ครั้ง แล้วเปิดแอป **Ubuntu** จากเมนู Start ของ Windows)*

**ขั้นที่ 2 (พิมพ์ในหน้าต่าง Ubuntu WSL2 เพื่อลงเครื่องมือทั้งหมด):**

```bash
sudo apt update
sudo apt install -y build-essential gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  qemu-system-arm python3 python3-matplotlib ffmpeg nginx curl git gh unzip screen
```

หลังจากติดตั้งเสร็จ เพื่อนที่ใช้ WSL2 สามารถใช้คำสั่งใน **หมวดที่ 3 ถึง หมวดที่ 8** ด้านล่างได้เหมือนเครื่อง Linux ทุกประการ

<<<<<<< HEAD
## Useful Docs
=======
## หมวดที่ 3: คำสั่งจัดการ Custom OS & Cloud Video (`make ...`)
>>>>>>> 9d6fa913760cf12d2d28eb88863f553258f44905

**Path ที่ต้อง `cd` เข้าไปก่อนเสมอ:**

```bash
cd ~/os_streaming/os-streaming-kernel
```

| คำสั่ง | ทำหน้าที่อะไร |
| :--- | :--- |
| `make cloud-pull` | **(สำคัญสำหรับเพื่อนที่เพิ่ง Clone ไป)** อ่านลิงก์ `cloud_url` ใน `www/db.json` แล้วดาวน์โหลดไฟล์วิดีโอขนาดใหญ่ (>100 MB เช่น `TEST1HR.mp4`, `WANNALISTEN2.mp4`) จาก GitHub Releases ลงมาไว้ใน `www/` และสร้าง `disk.img` อัตโนมัติ |
| `make cloud-push` | **(สำหรับคนอัปคลิปใหม่)** อัปโหลดไฟล์วิดีโอ `.mp4` ใน `www/` ขึ้นไปเก็บไว้บน GitHub Releases (`cloud-videos`), เขียนลิงก์ `cloud_url` ลง `www/db.json` อัตโนมัติ และซ่อนไฟล์ >95 MB ใน `.gitignore` |
| `make` หรือ `make all` | คอมไพล์ซอร์สโค้ด C และ Assembly ทั้งหมดเป็นไฟล์ `build/kernel.elf` (โดยยังไม่เปิด QEMU) |
| `make disk` | สแกนไฟล์วิดีโอใน `www/` เพื่ออัปเดต `www/db.json` อัตโนมัติ แล้วแพ็กโฟลเดอร์ `www/` ลงในอิมเมจดิสก์ `disk.img` (ขนาด 4 GB) |
| `make run` | คอมไพล์ Kernel + อัปเดต `db.json` & `disk.img` + เปิดรัน QEMU (พอร์ต `8080`) พร้อมเปิด Watcher เฝ้าโฟลเดอร์ `www/` อัตโนมัติทุก 2 วินาที |
| `make restart` | ปิด QEMU และ Cloudflare Tunnel ตัวเก่า (ถ้าค้างอยู่) แล้วสร้าง `disk.img` ใหม่ เปิดทั้ง **Custom OS Server (`:8080`)** และ **Cloudflare Tunnel (`*.trycloudflare.com`)** ขึ้นมาพร้อมกันในคำสั่งเดียว |
| `make clean` | ลบโฟลเดอร์ `build/` เพื่อบังคับคอมไพล์ซอร์สโค้ดใหม่ทั้งหมดตั้งแต่ต้น |
| `make debug` | เปิด QEMU ในโหมดรอรับการดีบักผ่าน GDB (`aarch64-linux-gnu-gdb build/kernel.elf -ex 'target remote :1234'`) |
| `sh scripts/faststart.sh` | จัดเรียง Header (`moov` atom) ของไฟล์ `.mp4` ทุกไฟล์ใน `www/` มาไว้ต้นไฟล์ด้วย `ffmpeg -c copy -movflags +faststart` เพื่อให้กรอและเริ่มเล่นคลิป 1 ชั่วโมงได้ทันที |
| `sh scripts/mkdisk.sh` | สั่งซิงก์ `www/db.json` กับไฟล์จริงใน `www/` และอัปเดต `disk.img` แบบ In-place ทันที |

## หมวดที่ 4: คำสั่งข้างใน Shell ของ Custom OS (`os> ...`)

**ตำแหน่งที่ใช้พิมพ์:** พิมพ์ในหน้าต่าง Terminal ที่รัน `make run` หรือ `make restart` หลังจากระบบบูตขึ้นบรรทัด `os> `

| คำสั่งใน `os>` | ทำหน้าที่อะไร |
| :--- | :--- |
| `help` | แสดงรายการคำสั่งทั้งหมดที่มีใน Shell ของ Kernel |
| `ls` | แสดงรายชื่อไฟล์และขนาดไฟล์ทั้งหมดใน `tarfs` (`disk.img`) โดยจะสแกน Header ล่าสุดให้อัตโนมัติ |
| `rescan` | สั่งบังคับสแกนรายการไฟล์ใน `disk.img` ใหม่ทันทีโดยไม่ต้องรีบูต Kernel |
| `ps` | แสดงรายการ Thread และ Process ทั้งหมด พร้อมสถานะ (`ready`/`sleep`), ระดับคิว MLFQ (`Q0`–`Q3`), จำนวนครั้งที่ถูกลดขั้น (`DEMOTE`) และเวลา CPU (`ticks`) |
| `sched mlfq` | สลับโหมดตัวจัดตารางงาน (Scheduler) เป็น **4-Level Priority MLFQ** (`Q0`=10ms, `Q1`=20ms, `Q2`=40ms, `Q3`=80ms + Priority Boost ทุก 2 วินาที) แบบสดๆ |
| `sched rr` | สลับโหมดตัวจัดตารางงานกลับเป็น **Preemptive Round-Robin** (Time slice 10 ms เท่ากันทุกเธรด) แบบสดๆ |
| `bench` | รันชุดทดสอบ **In-Kernel Scheduler Benchmark**: วัดเวลา Context-Switch (หน่วย ns และ cycles จาก `cntvct_el0`) และเปรียบเทียบค่า Jain's Fairness Index ระหว่าง Round-Robin กับ MLFQ |
| `audio status` | แสดงสถานะรีจิสเตอร์ของไดรเวอร์เสียงทั้ง 3 ระบบ (**RP1 PWM0**, **RP1 I2S0+DMA**, และ **BCM2712 `vc4-hdmi`**) พร้อมค่า Audio Master Clock |
| `audio pwm` | ทดสอบส่งสัญญาณเสียงความถี่ 440 Hz ผ่านไดรเวอร์ **RP1 PWM0 Audio** (`0x1f00098000`, Carrier 48 kHz, Range 1041 ticks) |
| `audio i2s` | ทดสอบส่งสัญญาณเสียงความถี่ 880 Hz 24-bit ผ่านไดรเวอร์ **RP1 DesignWare I2S0 + DMA Ring Buffer** (`0x1f000a0000`, BCLK 3.072 MHz สำหรับชิป DAC PCM5102A) |
| `audio hdmi` | ทดสอบส่งสัญญาณเสียงความถี่ 1000 Hz 24-bit ผ่านไดรเวอร์ **BCM2712 VideoCore VII `vc4-hdmi`** (`0x107c701400`: ตรวจสอบ EDID CEA-861 SAD, ตั้งค่า ACR `N=6144 / CTS=74250`, สร้าง Audio InfoFrame `0x84` Checksum `0x70` และส่ง IEC60958 Subframes เข้า MAI FIFO) |
| `audio all` | รันทดสอบไดรเวอร์เสียงครบทั้ง 3 สถาปัตยกรรม (**PWM0 + I2S0 + `vc4-hdmi`**) ต่อเนื่องกันในคำสั่งเดียว |
| `avsync test` | รันชุดทดสอบ **MJPEG Baseline Video Decoder (8x8 IDCT + YCbCr$\rightarrow$ARGB8888) + Kernel Timer A/V Sync Engine**: แสดงตารางเปรียบเทียบ `PTS_VID(ms)`, `MASTER_CLK(ms)`, `DRIFT(us)` และการตัดสินใจ `PRESENT`, `SLEEP_HOLD` (เมื่อเฟรมมาเร็วเกิน -15ms) และ `DROP_FRAME` (เมื่อเฟรมช้าเกิน +40ms) |
| `avsync clean` | รันชุดทดสอบ MJPEG + A/V Sync แบบสภาวะปกติ (ไม่มีการจำลองโหลดกระชาก) |
| `run` | สร้างและรัน User Process (ระดับสิทธิ์ `EL0`) จำนวน 1 ตัว |
| `run 3` | สร้างและรัน User Process (`EL0`) พร้อมกัน 3 ตัว (สามารถรัน `run 3` แล้วรีบพิมพ์ `ps` เพื่อดูการลดระดับคิว `Q0 -> Q3` ของ MLFQ ได้) |
| `crash` | ทดสอบระบบป้องกันหน่วยความจำ (Memory Protection): รันโปรแกรม `EL0` ที่พยายามเขียนทับหน่วยความจำของ Kernel (`0x40000000`) เพื่อให้เห็นว่า Kernel ดักจับ Exception และฆ่าเฉพาะโปรเซสนั้นทิ้งโดยที่ระบบไม่ล่ม |
| `mem` | แสดงจำนวนหน้าหน่วยความจำว่าง (Physical Page Allocator) และปริมาณการใช้ Kernel Heap |
| `uptime` | แสดงเวลาที่ Kernel ทำงานมาแล้วตั้งแต่บูต (วินาที) |
| `ifconfig` | แสดงหมายเลข IP Address ของ Kernel (`10.0.2.15`) |
| `cat <filename>` | แสดงเนื้อหาไฟล์ข้อความใน `tarfs` เช่น `cat index.html` หรือ `cat db.json` |
| `restart` หรือ `reboot` | สั่งซิงก์ `www/db.json` + `disk.img` ใหม่, รีสตาร์ท Cloudflare Tunnel และสั่งรีบูต Kernel ผ่าน PSCI `SYSTEM_RESET` ทันที |
| กด `Ctrl+A` แล้วปล่อย จากนั้นกด `x` | ปิดและออกจากโปรแกรมจำลอง QEMU กลับสู่ Linux Terminal |

## หมวดที่ 5: HTTP Endpoints ของ Custom OS Server (พอร์ต `8080`)

เมื่อรัน `make run` แล้ว สามารถเปิดเบราว์เซอร์หรือใช้ `curl` เรียกดู Endpoint ต่างๆ ได้ดังนี้:

| URL / Endpoint | ทำหน้าที่อะไร |
| :--- | :--- |
| `http://localhost:8080/` | หน้าเว็บสตรีมมิงหลัก **OS NatFlix** (รองรับค้นหา กรองหมวดหมู่ เปลี่ยนขนาด Buffer Chunk และสั่งสแกนไฟล์ใหม่) |
| `http://localhost:8080/library` | หน้าคลังไฟล์แบบ HTML ดั้งเดิมของ Kernel |
| `http://localhost:8080/status` | แสดงสถานะ Uptime, IP, หน่วยความจำว่าง, โหมด Scheduler และขนาด Buffer Chunk ปัจจุบัน |
| `http://localhost:8080/api/videos` | คืนค่ารายการไฟล์วิดีโอและเพลงทั้งหมดที่อยู่ใน `tarfs` เป็น JSON |
| `http://localhost:8080/api/audio` | คืนค่าสถานะรีจิสเตอร์ของไดรเวอร์เสียง Bare-Metal ทั้ง 3 ระบบ (`pwm`, `i2s`, `vc4-hdmi`) เป็น JSON |
| `http://localhost:8080/api/avsync` | คืนค่าผลการถอดรหัส MJPEG และสถิติความคลาดเคลื่อน (Drift) ของระบบ A/V Sync เป็น JSON |
| `http://localhost:8080/api/rescan` | สั่งให้ Kernel สแกนไฟล์ใน `disk.img` ใหม่ทันทีโดยไม่ต้องรีบูต |
| `http://localhost:8080/api/chunk?kb=1024` | ปรับขนาด HTTP 206 Range Buffer Chunk แบบสดๆ (เช่น `512`, `1024`, `2048`, `4096` KB) |
| `http://localhost:8080/api/restart` | สั่งรีสตาร์ททั้ง QEMU Kernel Server และ Cloudflare Tunnel ผ่านหน้าเว็บ |

## หมวดที่ 6: คำสั่ง Cloudflare Tunnel (เปิดเว็บให้เครื่องอื่น/มือถือเข้าผ่านอินเทอร์เน็ต)

**Path ที่ต้อง `cd` เข้าไปก่อน:**

```bash
cd ~/os_streaming/os-streaming-kernel
```

### 6.1 ติดตั้ง `cloudflared` (ทำครั้งแรกครั้งเดียวถ้าในเครื่องยังไม่มี)

* **บน Ubuntu / Debian / WSL2 (x86_64):**
  ```bash
  curl -L https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64.deb -o /tmp/cloudflared.deb
  sudo dpkg -i /tmp/cloudflared.deb
  ```
* **บนบอร์ด Raspberry Pi 5 (ARM64):**
  ```bash
  curl -L https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-arm64.deb -o /tmp/cloudflared.deb
  sudo dpkg -i /tmp/cloudflared.deb
  ```
* **บน Windows (PowerShell):**
  ```powershell
  winget install --id Cloudflare.cloudflared
  ```

### 6.2 คำสั่งรัน Cloudflare Tunnel

| คำสั่ง | ทำหน้าที่อะไร |
| :--- | :--- |
| `cloudflared tunnel --url http://127.0.0.1:8080 --protocol http2 --edge-ip-version 4` | เปิดลิงก์สาธารณะ `https://....trycloudflare.com` ชี้มายังพอร์ต `8080` ของ Custom OS (ต้องเปิด `make run` ค้างไว้ในอีก Terminal ก่อน) |
| `cloudflared tunnel --url http://127.0.0.1:8081 --protocol http2 --edge-ip-version 4` | เปิดลิงก์สาธารณะชี้ไปยังพอร์ต `8081` ของระบบที่ 1 (Linux Nginx HLS Server) |
| `make restart` | รันทั้ง Custom OS (`:8080`) และเปิด Cloudflare Tunnel ให้อัตโนมัติในหน้าต่างเดียวกัน |

## หมวดที่ 7: คำสั่งระบบที่ 1 — Linux + Raspberry Pi 5 (Nginx + HLS + OS Tuning + Benchmark)

**Path ที่ต้อง `cd` เข้าไปก่อนเสมอสำหรับหมวดนี้:**

```bash
cd ~/os_streaming/os-streaming-kernel/linux-pi5
```

| คำสั่งใน `linux-pi5/` | ทำหน้าที่อะไร |
| :--- | :--- |
| `make setup` | ติดตั้งแพ็กเกจสำหรับระบบที่ 1 ได้แก่ `nginx`, `ffmpeg`, `curl` และ `python3-matplotlib` |
| `make hls` | ใช้ `ffmpeg` แปลงไฟล์วิดีโอจาก `../www/` ออกเป็น HLS หลายความละเอียด (`360p` และ `720p` `.m3u8` + `.ts` segments) พร้อมไฟล์ `master.m3u8` ไว้ใน `linux-pi5/media/` |
| `make hls VIDEO=../www/TEST1HR.mp4` | สั่งแปลงไฟล์วิดีโอที่ระบุเป็น HLS หลายความละเอียด (`360p` + `720p`) |
| `make server` | เปิดรัน **Nginx จริง** (ตั้งค่า `epoll`, `sendfile on`, `tcp_nopush on`) บนพอร์ต `8081` (เข้าเว็บได้ที่ `http://localhost:8081`) |
| `make stop` | ปิด Nginx Server ที่รันอยู่บนพอร์ต `8081` |
| `make tune` | สั่งปรับจูน Linux OS สำหรับ Nginx ได้แก่ตั้งค่า `renice -n -10` (Scheduler priority), สร้าง `cgroups v2` (`/sys/fs/cgroup/os_streaming`) จำกัด CPU/RAM และแสดงสถานะ `zram` กับ `Page Cache` |
| `make benchmark` | **(ต้องเปิด `make run` ของ Custom OS ค้างไว้ในอีกจอ)** รันทดสอบเทียบความเร็วระหว่าง **Linux Nginx (`:8081`)** กับ **Custom OS (`:8080`)** ด้วยไฟล์เล็ก (`sample.mp4` 154 KB) ที่ `1, 2, 4, 8, 16` Clients พร้อมสร้างตาราง `results/benchmark_report.md`, `.csv` และกราฟ `results/benchmark_chart.png` |
| `make benchmark-large` | รันทดสอบเทียบความเร็วด้วย **ไฟล์วิดีโอขนาดใหญ่สุดใน `www/`** (เช่น `TEST1HR.mp4` 343.60 MB) โดยสุ่มตำแหน่ง `Range` ขนาด 1 MB ทั่วทั้งไฟล์ (`--random-offset`) แล้วสร้างกราฟ `results/benchmark_large_chart.png` และ `results/benchmark_large_report.md` |
| `sudo make benchmark-large` | เหมือน `make benchmark-large` แต่ใช้สิทธิ์ `sudo` สั่งล้าง Linux Page Cache (`echo 3 > /proc/sys/vm/drop_caches`) ก่อนวัดผล เพื่อตัดความได้เปรียบของ RAM Cache บนฝั่ง Linux |

## หมวดที่ 8: คำสั่งและการต่อวงจรบนบอร์ด Raspberry Pi 5 จริง (Bare-Metal UART, Framebuffer & Audio/Video)

### 8.1 ตารางการต่อสายวงจร (Pinout 40-Pin Header บน Raspberry Pi 5 — ลอจิก 3.3V เท่านั้น)

| อุปกรณ์ที่ต่อพ่วง | ขาของโมดูลภายนอก | ต่อเข้ากับขา Raspberry Pi 5 | หมายเลข GPIO / พอร์ต |
| :--- | :--- | :--- | :--- |
| **1. USB-to-TTL Serial Adapter** *(ตั้งแรงดันเป็น 3.3V)* | **GND** | **Pin 6** (GND) | Ground |
| | **RX / RXD** (ขารับของ Adapter) | **Pin 8** (UART0_TXD) | **GPIO 14** *(ต่อไขว้ TX $\rightarrow$ RX)* |
| | **TX / TXD** (ขาส่งของ Adapter) | **Pin 10** (UART0_RXD) | **GPIO 15** *(ต่อไขว้ RX $\leftarrow$ TX)* |
| | **VCC (5V/3.3V)** | **ห้ามต่อ (ปล่อยลอยไว้)** | บอร์ด Pi 5 รับไฟจาก USB-C อยู่แล้ว |
| **2. HDMI Video + HDMI Audio (`vc4-hdmi`)** | สาย Micro-HDMI to HDMI | ช่อง **HDMI 0** (ติดกับพอร์ต USB-C) | ส่งทั้งภาพ 1280x720p60 และเสียงดิจิทัลเข้าทีวีในสายเดียว |
| **3. โมดูล I2S DAC (PCM5102A)** | **VIN** | **Pin 1** (3.3V) หรือ **Pin 2** (5V) | ไฟเลี้ยงบอร์ด DAC |
| | **GND** และ **SCK** | **Pin 9** (GND) | เชื่อมขา SCK ลง GND เพื่อใช้ PLL ในตัวชิป |
| | **BCK** (Bit Clock) | **Pin 12** (I2S0_SCLK) | **GPIO 18** |
| | **LCK / WS** (Word Select) | **Pin 35** (I2S0_WS) | **GPIO 19** |
| | **DIN** (Data In) | **Pin 40** (I2S0_SDO0) | **GPIO 21** |
| **4. วงจรเสียง PWM Audio** | **Left (+)** / **Right (+)** / **GND** | **Pin 32** / **Pin 33** / **Pin 34** | **GPIO 12** (PWM0_0) / **GPIO 13** (PWM0_1) / **GND** |

### 8.2 คำสั่งคอมไพล์อิมเมจ Bare-Metal (`kernel8.img`) ทั้ง 3 ขั้นตอน

| ขั้นตอนทดสอบบน Pi 5 | Path ที่ต้อง `cd` เข้าไปก่อน | คำสั่งคอมไพล์และใช้งาน |
| :--- | :--- | :--- |
| **Step 0: ทดสอบ RP1 UART0 Serial** | `cd ~/os_streaming/os-streaming-kernel/pi5-uart-bringup` | รัน `aarch64-linux-gnu-gcc -ffreestanding -nostdlib -mgeneral-regs-only -c boot_uart_hello.S -o boot_uart_hello.o && aarch64-linux-gnu-ld -nostdlib -T linker_pi5.ld -o kernel8.elf boot_uart_hello.o && aarch64-linux-gnu-objcopy -O binary kernel8.elf kernel8.img` แล้วนำ `kernel8.img` คู่กับ `config.txt` ไปวางในพาร์ทิชัน `bootfs` |
| **Step 1: ทดสอบ HDMI Framebuffer 720p** | `cd ~/os_streaming/os-streaming-kernel/pi5-framebuffer-bringup` | รัน `make` เพื่อสร้าง `kernel8.img` สำหรับแสดงแถบสี SMPTE และกราฟิกออกจอ HDMI 1280x720 |
| **Step 2: ทดสอบรวมภาพ HDMI + เสียงครบ 3 แบบ (PWM/I2S/vc4-hdmi) + MJPEG A/V Sync** | `cd ~/os_streaming/os-streaming-kernel/pi5-av-bringup` | รัน `make` เพื่อสร้าง `kernel8.img` ตัวสมบูรณ์ แล้วนำไปวางคู่กับ `config.txt` บนพาร์ทิชัน `bootfs` ของการ์ด MicroSD |
| **เปิดหน้าจอรับข้อความ Serial บนคอมพิวเตอร์** | รันใน Terminal ของเครื่องที่เสียบสาย USB-to-TTL | `screen /dev/ttyUSB0 115200` (หรือใช้โปรแกรม PuTTY เลือก Serial `115200` 8N1) |
