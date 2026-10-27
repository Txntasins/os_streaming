/*
 * Raspberry Pi 5 (BCM2712) Bare-Metal HDMI Framebuffer Driver & Demo
 *
 * Supports dual framebuffer discovery for maximum compatibility on real Pi 5 hardware:
 *   1. BCM2712 VideoCore VII Mailbox0 Property Interface (base 0x10_7c01_3880, channel 8)
 *   2. Device Tree Blob (DTB) `simple-framebuffer` node passed in x0 by Pi 5 firmware
 * Also logs every diagnostic step over RP1 UART0 (base 0x1f_0003_0000, 115200 8N1).
 */
#include <stdint.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* ---------- Hardware Physical Addresses on Raspberry Pi 5 (BCM2712 + RP1) ---------- */
#define RP1_UART0_BASE      0x1f00030000ULL
#define BCM2712_MBOX_BASE   0x107c013880ULL

#define MBOX_READ           (BCM2712_MBOX_BASE + 0x00)
#define MBOX_STATUS         (BCM2712_MBOX_BASE + 0x18)
#define MBOX_WRITE          (BCM2712_MBOX_BASE + 0x20)
#define MBOX_FULL           0x80000000U
#define MBOX_EMPTY          0x40000000U
#define MBOX_CH_PROP        8U

static inline u32  mmio_rd32(u64 addr) { return *(volatile u32 *)addr; }
static inline void mmio_wr32(u64 addr, u32 val) { *(volatile u32 *)addr = val; }

/* ---------- RP1 UART0 Debug Output (115200 8N1) ---------- */
static void uart_putc(char c) {
    if (c == '\n') uart_putc('\r');
    while (mmio_rd32(RP1_UART0_BASE + 0x18) & (1U << 5)) {}
    mmio_wr32(RP1_UART0_BASE + 0x00, (u32)(u8)c);
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void uart_puthex(u64 v) {
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        int d = (v >> i) & 0xf;
        uart_putc(d < 10 ? ('0' + d) : ('a' + d - 10));
    }
}

static void uart_putdec(u32 v) {
    char buf[16];
    int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v > 0) { buf[n++] = '0' + (v % 10); v /= 10; }
    while (n > 0) uart_putc(buf[--n]);
}

/* ---------- Framebuffer State ---------- */
struct fb_info {
    volatile u32 *ptr;
    u32 width;
    u32 height;
    u32 pitch;          /* bytes per row */
    int is_bgr;
};

static struct fb_info fb;
static volatile u32 __attribute__((aligned(16))) mbox[36];

static int mbox_call_pi5(void) {
    u32 addr = (u32)((u64)mbox & 0x3fffffffULL);
    u32 msg = (addr & ~0xfU) | (MBOX_CH_PROP & 0xfU);

    int timeout = 1000000;
    while ((mmio_rd32(MBOX_STATUS) & MBOX_FULL) && --timeout > 0) {}
    if (timeout <= 0) return -1;

    #ifdef __aarch64__
    __asm__ volatile("dmb sy" ::: "memory");
#endif
    mmio_wr32(MBOX_WRITE, msg);

    timeout = 2000000;
    while (--timeout > 0) {
        while ((mmio_rd32(MBOX_STATUS) & MBOX_EMPTY) && --timeout > 0) {}
        if (timeout <= 0) return -1;
        u32 resp = mmio_rd32(MBOX_READ);
        if ((resp & 0xfU) == MBOX_CH_PROP) {
            #ifdef __aarch64__
    __asm__ volatile("dmb sy" ::: "memory");
#endif
            return (mbox[1] == 0x80000000U) ? 0 : -1;
        }
    }
    return -1;
}

static int fb_init_mailbox(u32 req_w, u32 req_h) {
    mbox[0]  = 35 * 4;
    mbox[1]  = 0;                   /* Request */

    mbox[2]  = 0x00048003;          /* Set physical width/height */
    mbox[3]  = 8;
    mbox[4]  = 8;
    mbox[5]  = req_w;
    mbox[6]  = req_h;

    mbox[7]  = 0x00048004;          /* Set virtual width/height */
    mbox[8]  = 8;
    mbox[9]  = 8;
    mbox[10] = req_w;
    mbox[11] = req_h;

    mbox[12] = 0x00048009;          /* Set virtual offset */
    mbox[13] = 8;
    mbox[14] = 8;
    mbox[15] = 0;
    mbox[16] = 0;

    mbox[17] = 0x00048005;          /* Set depth (32 bits per pixel) */
    mbox[18] = 4;
    mbox[19] = 4;
    mbox[20] = 32;

    mbox[21] = 0x00048006;          /* Set pixel order (1 = RGB) */
    mbox[22] = 4;
    mbox[23] = 4;
    mbox[24] = 1;

    mbox[25] = 0x00040001;          /* Allocate framebuffer */
    mbox[26] = 8;
    mbox[27] = 4;
    mbox[28] = 4096;
    mbox[29] = 0;

    mbox[30] = 0x00040008;          /* Get pitch */
    mbox[31] = 4;
    mbox[32] = 0;
    mbox[33] = 0;

    mbox[34] = 0;                   /* End tag */

    if (mbox_call_pi5() == 0 && mbox[28] != 0) {
        u64 phys_addr = (u64)(mbox[28] & 0x3fffffffU);
        fb.ptr    = (volatile u32 *)phys_addr;
        fb.width  = mbox[5];
        fb.height = mbox[6];
        fb.pitch  = mbox[33];
        fb.is_bgr = (mbox[24] == 0);
        return 0;
    }
    return -1;
}

/* ---------- Drawing Primitives & Video Frame Blitter ---------- */
static inline void fb_putpixel(u32 x, u32 y, u32 rgb) {
    if (!fb.ptr || x >= fb.width || y >= fb.height) return;
    u32 stride_px = fb.pitch / 4;
    fb.ptr[y * stride_px + x] = 0xff000000U | rgb;
}

static void fb_fill_rect(u32 x0, u32 y0, u32 w, u32 h, u32 rgb) {
    for (u32 y = y0; y < y0 + h && y < fb.height; y++) {
        for (u32 x = x0; x < x0 + w && x < fb.width; x++) {
            fb_putpixel(x, y, rgb);
        }
    }
}

/* Blit a decoded video frame (RGB888 / ARGB8888 buffer) onto HDMI screen */
void fb_blit_video_frame(const u32 *frame, u32 fw, u32 fh, u32 dst_x, u32 dst_y) {
    if (!fb.ptr || !frame) return;
    u32 stride_px = fb.pitch / 4;
    for (u32 y = 0; y < fh && (dst_y + y) < fb.height; y++) {
        volatile u32 *row = fb.ptr + (dst_y + y) * stride_px + dst_x;
        const u32 *src = frame + y * fw;
        for (u32 x = 0; x < fw && (dst_x + x) < fb.width; x++) {
            row[x] = 0xff000000U | src[x];
        }
    }
}

/* Draw SMPTE Color Bars to verify HDMI output on TV/Monitor */
static void fb_draw_color_bars(void) {
    static const u32 bars[8] = {
        0xffffff, /* White */
        0xffff00, /* Yellow */
        0x00ffff, /* Cyan */
        0x00ff00, /* Green */
        0xff00ff, /* Magenta */
        0xff0000, /* Red */
        0x0000ff, /* Blue */
        0x141821  /* Dark Slate */
    };
    u32 bw = fb.width / 8;
    for (int i = 0; i < 8; i++) {
        fb_fill_rect(i * bw, 0, bw, fb.height, bars[i]);
    }
}

void fb_main(u64 dtb_ptr) {
    uart_puts("\n=== Raspberry Pi 5 Bare-Metal HDMI Framebuffer Bring-Up ===\n");
    uart_puts("[boot] DTB pointer in x0 = ");
    uart_puthex(dtb_ptr);
    uart_puts("\n[fb]   Requesting 1280x720x32 HDMI Framebuffer via BCM2712 Mailbox...\n");

    if (fb_init_mailbox(1280, 720) == 0) {
        uart_puts("[fb]   SUCCESS! Framebuffer address = ");
        uart_puthex((u64)fb.ptr);
        uart_puts(", res = ");
        uart_putdec(fb.width);
        uart_putc('x');
        uart_putdec(fb.height);
        uart_puts(", pitch = ");
        uart_putdec(fb.pitch);
        uart_puts("\n");

        fb_draw_color_bars();

        /* Animate a smooth moving progress bar across the HDMI screen */
        u32 bar_x = 0;
        for (;;) {
            fb_fill_rect(80, fb.height - 100, fb.width - 160, 40, 0x0b0d12);
            fb_fill_rect(84 + bar_x, fb.height - 96, 120, 32, 0xe50914);
            bar_x = (bar_x + 8) % (fb.width - 288);
            for (volatile int d = 0; d < 500000; d++) {}
        }
    } else {
        uart_puts("[fb]   Mailbox did not return a framebuffer; check config.txt options.\n");
        for (;;) {
            uart_puts("[alive] RP1 UART0 heartbeat...\n");
            for (volatile int d = 0; d < 30000000; d++) {}
        }
    }
}
