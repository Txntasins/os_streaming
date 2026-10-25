.RECIPEPREFIX = >
CROSS ?= aarch64-linux-gnu-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
CFLAGS  = -std=gnu11 -ffreestanding -nostdlib -O2 -Wall -Wextra -mgeneral-regs-only -fno-pic

all: kernel8.img

boot_fb.o: boot_fb.S
> $(CC) $(CFLAGS) -c $< -o $@

fb_pi5.o: fb_pi5.c
> $(CC) $(CFLAGS) -c $< -o $@

kernel8.elf: boot_fb.o fb_pi5.o linker_pi5.ld
> $(LD) -nostdlib --build-id=none -T linker_pi5.ld -o $@ boot_fb.o fb_pi5.o

kernel8.img: kernel8.elf
> $(OBJCOPY) -O binary $< $@
> @echo "Built Pi 5 HDMI Framebuffer kernel8.img ($(shell stat -c%s $@) bytes)"

clean:
> rm -f *.o *.elf kernel8.img

.PHONY: all clean
