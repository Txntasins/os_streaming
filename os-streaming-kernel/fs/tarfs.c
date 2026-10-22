/* tarfs: disk = tar file (ustar) -> read files directly by knowing their offsets, no complex filesystem structure needed */
#include "kernel.h"
#include <string.h>

#define MAX_FILES 64
static struct tar_file files[MAX_FILES];
static int nfiles;
static u8 *bounce;                          /* 64 KB disk read buffer */
#define BOUNCE_SECTORS 128

static u64 parse_octal(const char *s, int n) {
    u64 v = 0;
    for (int i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++) v = v * 8 + (s[i] - '0');
    return v;
}

/* Re-scans the disk's directory (file list) without rebooting. Safe to call
   again at runtime: the admin reruns scripts/mkdisk.sh on the host (same
   fixed-size disk.img, same backing inode QEMU already has open), then types
   "rescan" in the shell. Existing playback in progress is unaffected: it
   already holds onto its own (struct tar_file *) and offset. */
int tarfs_rescan(void) {
    if (!blk_sectors()) return -1;
    nfiles = 0;
    u64 sector = 0, total = blk_sectors();
    static u8 hdr[512] __attribute__((aligned(16)));
    while (sector < total && nfiles < MAX_FILES) {
        if (blk_read(sector, 1, hdr) < 0) break;
        if (hdr[0] == 0) break;                                   /* Zero block = end of archive */
        u64 size = parse_octal((char *)hdr + 124, 12);
        char type = hdr[156];
        if (type == '0' || type == 0) {
            char name[101]; memcpy(name, hdr, 100); name[100] = 0;
            const char *n = name;
            while (n[0] == '.' && n[1] == '/') n += 2;
            struct tar_file *f = &files[nfiles++];
            strncpy(f->name, n, 99); f->name[99] = 0;
            f->size = size;
            f->offset = (sector + 1) * 512;
        }
        sector += 1 + (size + 511) / 512;
    }
    kprintf("[fs]   tarfs: found %d files\n", nfiles);
    for (int i = 0; i < nfiles; i++) kprintf("         %10lu  %s\n", (unsigned long)files[i].size, files[i].name);
    return nfiles;
}

int tarfs_init(void) {
    if (!blk_sectors()) return -1;
    bounce = pmm_alloc(BOUNCE_SECTORS * 512 / PAGE_SIZE);   /* one-time: never re-alloc on rescan */
    if (!bounce) return -1;
    return tarfs_rescan();
}

int tarfs_count(void) { return nfiles; }
const struct tar_file *tarfs_get(int i) { return (i >= 0 && i < nfiles) ? &files[i] : 0; }
const struct tar_file *tarfs_find(const char *name) {
    while (*name == '/') name++;
    for (int i = 0; i < nfiles; i++) if (!strcmp(files[i].name, name)) return &files[i];
    return 0;
}

/* Read len bytes from offset off of the file (returns the number of bytes read) */
int tarfs_read(const struct tar_file *f, u64 off, void *buf, size_t len) {
    if (off >= f->size) return 0;
    if (len > f->size - off) len = f->size - off;
    size_t done = 0;
    while (done < len) {
        u64 abs = f->offset + off + done;
        u64 sec = abs / 512; unsigned skip = abs % 512;
        size_t want = len - done;
        unsigned nsec = (skip + want + 511) / 512;
        if (nsec > BOUNCE_SECTORS) nsec = BOUNCE_SECTORS;
        if (blk_read(sec, nsec, bounce) < 0) return -1;
        size_t avail = nsec * 512 - skip;
        size_t n = want < avail ? want : avail;
        memcpy((u8 *)buf + done, bounce + skip, n);
        done += n;
    }
    return (int)done;
}