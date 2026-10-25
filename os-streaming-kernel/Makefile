.RECIPEPREFIX = >
CROSS ?= aarch64-linux-gnu-
CC     = $(CROSS)gcc
LD     = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
BUILD  = build

CFLAGS  = -std=gnu11 -ffreestanding -nostdlib -O2 -Wall -Wextra -Wno-unused-parameter -mgeneral-regs-only \
          -fno-tree-loop-distribute-patterns -fno-stack-protector -fno-pic -g \
          -MMD -MP -Iinclude -Ilibc/include
LDFLAGS = -nostdlib --build-id=none -T linker.ld --no-warn-rwx-segments

SRC_DIRS = arch kernel drivers mm fs net libc
KSRC     = $(shell find $(SRC_DIRS) -name '*.c' -o -name '*.S')
LWIP     = third_party/lwip/src
LWSRC    = $(wildcard $(LWIP)/core/*.c) $(wildcard $(LWIP)/core/ipv4/*.c) $(LWIP)/netif/ethernet.c
OBJS     = $(patsubst %,$(BUILD)/%.o,$(KSRC) $(LWSRC))

QEMU       = qemu-system-aarch64
QEMU_FLAGS = -M virt,gic-version=2 -cpu cortex-a72 -m 256M -nographic \
             -drive file=disk.img,if=none,format=raw,cache=writethrough,id=hd0 -device virtio-blk-device,drive=hd0 \
             -netdev user,id=net0,hostfwd=tcp::8080-:80 -device virtio-net-device,netdev=net0

all: $(BUILD)/kernel.elf

$(BUILD)/kernel.elf: $(OBJS) linker.ld
> $(LD) $(LDFLAGS) -o $@ $(OBJS)

# ---- user programs ----
$(BUILD)/user/hello.elf: user/start.S user/hello.c user/user.ld
> @mkdir -p $(dir $@)
> $(CC) -ffreestanding -nostdlib -O2 -mgeneral-regs-only -fno-pic -static -Wl,--build-id=none -Wl,--no-warn-rwx-segments -T user/user.ld -Ilibc/include -o $@ user/start.S user/hello.c
$(BUILD)/user/hello.bin: $(BUILD)/user/hello.elf
> $(OBJCOPY) -O binary $< $@
$(BUILD)/kernel/userprog.S.o: $(BUILD)/user/hello.bin

$(BUILD)/third_party/%.c.o: third_party/%.c
> @mkdir -p $(dir $@)
> $(CC) $(CFLAGS) -Ithird_party/lwip/src/include -Inet -w -c $< -o $@

$(BUILD)/%.c.o: %.c
> @mkdir -p $(dir $@)
> $(CC) $(CFLAGS) -Ithird_party/lwip/src/include -Inet -c $< -o $@

$(BUILD)/%.S.o: %.S
> @mkdir -p $(dir $@)
> $(CC) $(CFLAGS) -c $< -o $@

disk.img:
> sh scripts/mkdisk.sh

disk: disk.img

cloud-pull:
> sh scripts/cloud-sync.sh

run: all
> sh scripts/run-all.sh "$(QEMU)" "$(BUILD)/kernel.elf"

# Restart both QEMU Server and Cloudflare Tunnel together in one command
restart: all
> pkill -f "qemu-system-aarch64.*kernel.elf" 2>/dev/null || true
> pkill -x cloudflared 2>/dev/null || true
> WITH_TUNNEL=1 sh scripts/run-all.sh "$(QEMU)" "$(BUILD)/kernel.elf"

debug: all disk.img
> @echo "Open another terminal and run: aarch64-linux-gnu-gdb build/kernel.elf -ex 'target remote :1234'"
> $(QEMU) $(QEMU_FLAGS) -kernel $(BUILD)/kernel.elf -s -S

-include $(OBJS:.o=.d)

clean:
> rm -rf $(BUILD)

.PHONY: all disk disk.img cloud-pull run restart debug clean
