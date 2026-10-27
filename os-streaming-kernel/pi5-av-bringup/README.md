# Raspberry Pi 5 Bare-Metal Audio (PWM + I2S + HDMI `vc4-hdmi`) & MJPEG A/V Sync (`pi5-av-bringup/`)

โฟลเดอร์นี้คือชุดโค้ด Bare-Metal สำหรับรันบนบอร์ด **Raspberry Pi 5 จริง (`kernel8.img`)** ที่รวมครบทั้ง:
1. **RP1 UART0 (`0x1f00030000`)**: พิมพ์สถานะการบูตและค่า Drift ของ A/V Sync ออกสาย Serial (115200 8N1)
2. **BCM2712 VideoCore VII Mailbox Framebuffer (`0x107c013880`)**: เปิดหน้าจอ HDMI 1280x720 (32-bit ARGB)
3. **RP1 PWM0 Audio (`0x1f00098000`)**: ส่งสัญญาณเสียง Stereo PWM (Carrier 48 kHz, Range 1041 ticks) ออกขา GPIO 12/13 หรือ 18/19
4. **RP1 DesignWare I2S0 + DMA (`0x1f000a0000`)**: ส่งสัญญาณเสียงดิจิทัล 24-bit/48kHz ออกโมดูล I2S DAC (เช่น PCM5102A)
5. **BCM2712 VideoCore VII `vc4-hdmi` Audio (`0x107c701400`)**:
   - ตั้งค่า **Clock Regeneration Packet (ACR)**: `N = 6144`, `CTS = 74250` (สำหรับ 48 kHz บนความละเอียด 720p60 TMDS 74.25 MHz)
   - เขียน **CEA-861-D Audio InfoFrame (`0x84`)** พร้อมคำนวณ Checksum (`0x70`) ลงใน HDMI Packet RAM (`0x107c702490`)
   - เข้ารหัส **IEC60958 / S/PDIF Subframes** (Preamble `B/M/W`, ข้อมูลเสียง 24-bit PCM, Channel Status 192-bit และ Even Parity bit) ส่งเข้า `VC4_HD_MAI_DATA` FIFO
6. **MJPEG Video + Kernel Timer A/V Sync (`cntvct_el0`)**: ถอดรหัสเฟรมภาพและซิงก์เวลาแสดงผลกับสัญญาณเสียงด้วยฮาร์ดแวร์ไทม์เมอร์ของ ARM64

## วิธีคอมไพล์และนำไปรันบน Raspberry Pi 5

```bash
cd ~/os_streaming/os-streaming-kernel/pi5-av-bringup
make
```

จากนั้นคัดลอกไฟล์ `kernel8.img` และ `config.txt` ไปวางในพาร์ทิชัน `bootfs` (FAT32) ของการ์ด MicroSD แล้วเสียบเข้าบอร์ด Raspberry Pi 5 ที่ต่อสาย HDMI เข้าทีวี/จอมอนิเตอร์ได้เลย
