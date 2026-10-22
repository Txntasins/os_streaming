#!/bin/sh
# Create disk.img = tar (ustar) of the www/ directory -> kernel reads files directly via virtio-blk
#
# Fixed-size: always padded to DISK_SIZE (default 4GiB) with a sparse hole past the real
# content. tarfs stops scanning at the first zero block, so the padding is invisible to it.
# This lets you re-run this script while QEMU is already running (add/replace a video in
# www/, rerun this, then type "rescan" in the booted shell) WITHOUT restarting the VM —
# virtio-blk's reported capacity is fixed at boot time from the file's size at that moment,
# so growing past DISK_SIZE later still requires a restart; pick it generously up front.
set -e
cd "$(dirname "$0")/.."
[ -d www ] || { echo "Directory www/ not found"; exit 1; }
DISK_SIZE="${DISK_SIZE:-4G}"
tar --format=ustar --owner=0 --group=0 --numeric-owner -cf disk.img -C www .
truncate -s "$DISK_SIZE" disk.img
echo "Created disk.img (content: $(du -h --apparent-size disk.img | cut -f1) padded, $DISK_SIZE total): $(ls www | tr '\n' ' ')"
