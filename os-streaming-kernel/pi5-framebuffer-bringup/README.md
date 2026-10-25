# Raspberry Pi 5 Bare-Metal Bring-Up — Step 1: HDMI Framebuffer + Video Blitter

This standalone module initializes a **1280x720 32-bit ARGB HDMI Framebuffer** on Raspberry Pi 5 (BCM2712) via the VideoCore VII Mailbox Property interface (`0x107c013880`, channel 8) while logging diagnostics over **RP1 UART0 (`0x1f00030000`)**.

## Build `kernel8.img`
```bash
cd pi5-framebuffer-bringup
make
```

## Deploy to SD Card
Copy `config.txt` and `kernel8.img` to the FAT32 `bootfs` partition of the Raspberry Pi 5 SD card, connect HDMI and USB-to-TTL serial (pins 6, 8, 10 at 115200 8N1), and power on.
