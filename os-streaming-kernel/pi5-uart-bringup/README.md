# Pi 5 bring-up, step 0: prove UART output works

**Status: written and verified as far as possible without a real Pi 5 — assembled,
linked, and the UART address was confirmed byte-for-byte in the compiled binary.
It has never been run on actual hardware. Treat the first boot as an experiment,
not a sure thing.**

## Why this step, and only this step, first

Everything else on a Pi 5 — SD card, Ethernet, USB, audio — is reached through
**RP1**, a separate chip connected over PCIe, not simple memory-mapped registers
on the SoC like a Pi 3/4. If your first attempt at any of those silently hangs,
you have no way to tell whether the bug is in that driver or in something more
basic (boot, RP1 reachability, UART wiring). Getting a single line of text out
over UART first gives you a debug channel for everything that comes after.

Source for every address used here: the `rpi-hal` crate's hand-written BCM2712
register map, which cites Raspberry Pi's own `bcm2712.dtsi`/`rp1.dtsi` as its
source — https://docs.rs/crate/rpi-hal/latest/source/src/pac_bcm2712.rs

## What you need

- A Raspberry Pi 5
- A **USB-to-TTL serial adapter** (3.3V logic level — 5V will damage the Pi).
  Without one you will see **nothing**: Pi 5 has no bare-metal video output
  until a framebuffer driver exists (not written yet), so this UART is your
  only window into the machine for now.
- 3 jumper wires

## Wiring (from the rpi-hal crate's own setup guide)

| Adapter pin | Connects to |
|---|---|
| GND | Any Ground pin on the 40-pin header (e.g. physical pin 6) |
| RX (adapter in) | GPIO14 / physical pin 8 (Pi's TX) |
| TX (adapter out) | GPIO15 / physical pin 10 (Pi's RX) |

## Build

```bash
aarch64-linux-gnu-gcc -ffreestanding -nostdlib -mgeneral-regs-only -c boot_uart_hello.S -o boot_uart_hello.o
aarch64-linux-gnu-ld -nostdlib -T linker_pi5.ld -o kernel8.elf boot_uart_hello.o
aarch64-linux-gnu-objcopy -O binary kernel8.elf kernel8.img
```

## Put on the SD card

1. Flash a normal Raspberry Pi OS image first with Raspberry Pi Imager (this gives you
   the firmware files — `bootcode.bin` equivalents, `start4.elf`, etc. — that boot_uart_hello.S
   depends on; you are not replacing the firmware, only the kernel it loads).
2. On the boot partition, replace `config.txt` with the one in this folder, and copy
   `kernel8.img` there too.

## Run it

On your computer, open the serial port at **115200 8N1** (the enable_rp1_uart
firmware option sets this baud rate automatically — do not change it):
```bash
# Linux/Mac, adjust the device name:
screen /dev/ttyUSB0 115200
# or: minicom -D /dev/ttyUSB0 -b 115200
```
Power on the Pi. You should see:
```
Hello from the Pi 5 (RP1 UART0)!
Hello from the Pi 5 (RP1 UART0)!
...
```
repeating every couple of seconds.

## If you see nothing at all (the most likely failure)

In order of likelihood:
1. **`config.txt` wasn't actually replaced**, or `pciex4_reset=0` / `enable_rp1_uart=1`
   is missing a line, mistyped, or the file has Windows line endings that the
   firmware's parser chokes on. Re-check it byte for byte.
2. **Wiring**: TX/RX are swapped (very common), or the adapter is 5V not 3.3V.
3. **Serial program settings**: wrong baud rate, wrong device path, or another
   program already has the port open.
4. **`kernel_address` mismatch**: if the firmware doesn't honor this option the
   way I expect on your specific firmware version, the CPU starts executing at
   the wrong address entirely. If nothing above fixes it, this is the next
   thing to question — tell me your firmware version and I'll help dig further.

Whatever happens, tell me exactly what you see (or don't see) and I'll help
debug from there. This is real hardware bring-up — it is normal for the first
attempt not to work.
