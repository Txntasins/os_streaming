CROSS ?= aarch64-linux-gnu-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy

CFLAGS  = -std=gnu11 -ffreestanding -nostdlib -O2 -Wall -Wextra -mgeneral-regs-only -fno-pic -fno-stack-protector

all: kernel8.img

boot_av.o: boot_av.S
	$(CC) $(CFLAGS) -c boot_av.S -o boot_av.o

pi5_av_player.o: pi5_av_player.c
	$(CC) $(CFLAGS) -c pi5_av_player.c -o pi5_av_player.o

kernel8.elf: boot_av.o pi5_av_player.o linker_pi5.ld
	$(LD) -nostdlib --build-id=none -T linker_pi5.ld -o kernel8.elf boot_av.o pi5_av_player.o

kernel8.img: kernel8.elf
	$(OBJCOPY) -O binary kernel8.elf kernel8.img
	@echo "[OK] Built Raspberry Pi 5 Bare-Metal Audio+Video image: kernel8.img"

clean:
	rm -f *.o *.elf kernel8.img

.PHONY: all clean
