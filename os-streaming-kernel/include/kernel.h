#ifndef KERNEL_H
#define KERNEL_H
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* ---------- Memory Map of QEMU "virt" Machine ---------- */
#define RAM_BASE        0x40000000UL
#define RAM_SIZE        (256UL << 20)           /* QEMU must be run with -m 256M */
#define USER_PHYS_BASE  0x4F000000UL            /* The last 16 MB of RAM is reserved for user processes */
#define USER_SLOT_SIZE  (2UL << 20)             /* 2 MB per process */
#define USER_SLOTS      8
#define USER_VBASE      0x80000000UL            /* Virtual address for all user programs */
#define USER_STACK_TOP  (USER_VBASE + USER_SLOT_SIZE)

#define PAGE_SIZE       4096UL

/* ---------- Scheduler Modes & MLFQ Constants ---------- */
#define SCHED_RR        0
#define SCHED_MLFQ      1
#define MLFQ_LEVELS     4                       /* Q0 (highest, 10ms) .. Q3 (lowest, 80ms) */
#define MLFQ_BOOST_TICKS 200                    /* Priority boost every 200 ticks (2.0 s) */

/* ---------- Audio Backend Modes (Pi 5 RP1 / BCM2712) ---------- */
#define AUDIO_BACKEND_PWM   0                   /* RP1 PWM0/PWM1 Audio (GPIO 12/13 or 18/19) */
#define AUDIO_BACKEND_I2S   1                   /* RP1 DesignWare I2S0 + DMA0 -> PCM5102A DAC */
#define AUDIO_BACKEND_HDMI  2                   /* BCM2712 VideoCore VII vc4-hdmi MAI + CEA-861 InfoFrame */

/* ---------- MMIO / IRQ ---------- */
static inline u32  rd32(u64 a)         { return *(volatile u32 *)a; }
static inline void wr32(u64 a, u32 v)  { *(volatile u32 *)a = v; }
static inline u8   rd8(u64 a)          { return *(volatile u8 *)a; }

static inline unsigned long irq_save(void) {
    unsigned long f = 0;
#ifdef __aarch64__
    __asm__ volatile("mrs %0, daif\n msr daifset, #2" : "=r"(f) :: "memory");
#endif
    return f;
}
static inline void irq_restore(unsigned long f) {
#ifdef __aarch64__
    __asm__ volatile("msr daif, %0" :: "r"(f) : "memory");
#else
    (void)f;
#endif
}
static inline void dmb(void) {
#ifdef __aarch64__
    __asm__ volatile("dmb sy" ::: "memory");
#endif
}

/* ---------- trap frame (saved by vectors.S) ---------- */
struct trapframe {
    u64 x[31];
    u64 elr, spsr, sp_el0, pad;
};

/* ---------- Our own small libc ---------- */
void kprintf(const char *fmt, ...);
int  ksnprintf(char *buf, size_t n, const char *fmt, ...);
int  kvsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
void panic(const char *msg) __attribute__((noreturn));

/* Trigger ARM64 PSCI SYSTEM_RESET (0x84000009) so QEMU & host supervisor restart cleanly */
static inline void sys_restart(void) {
    kprintf("\n[HOST_RESTART_REQUESTED]\n");
#ifdef __aarch64__
    __asm__ volatile(
        "movz x0, #0x0009\n"
        "movk x0, #0x8400, lsl #16\n"
        "hvc  #0\n"
        ::: "x0", "memory"
    );
    for (;;) __asm__ volatile("wfe");
#endif
}

/* ---------- drivers ---------- */
void uart_putc(char c);
int  uart_getc(void);                 /* -1 if no character is available */
void gic_init(void);
void gic_enable_irq(unsigned irq);
u32  gic_ack(void);
void gic_eoi(u32 iar);
void timer_init(unsigned hz);
void timer_rearm(void);
u64  uptime_ms(void);
u64  timer_ticks(void);

/* ---------- Bare-Metal Audio Subsystem (PWM / I2S / HDMI vc4-hdmi) ---------- */
void audio_init_all(void);
void audio_cmd(const char *arg);
int  audio_status_json(char *buf, size_t cap);
void audio_submit_pcm16(const int16_t *samples, u32 frames, u32 sample_rate);
u64  audio_MasterClock_us(void);

/* ---------- Bare-Metal MJPEG Video Decoder + Kernel Timer A/V Sync ---------- */
void avsync_cmd(const char *arg);
int  avsync_status_json(char *buf, size_t cap);

/* ---------- memory ---------- */
void  mmu_init(void);
u64  *mmu_kernel_table(void);
u64  *mmu_new_user_table(u64 user_phys);
void  mmu_switch(u64 *l1);
void  pmm_init(void);
void *pmm_alloc(size_t pages);          /* Contiguous pages, cleared to 0 */
void  pmm_free(void *p, size_t pages);
size_t pmm_total_pages(void);
size_t pmm_free_pages(void);
void  kheap_init(void);
void *kmalloc(size_t n);
void  kfree(void *p);
size_t kheap_used(void);

/* ---------- threads / process / scheduler ---------- */
void thread_init(void);
int  thread_create(void (*fn)(void *), void *arg, const char *name);
int  thread_create_prio(void (*fn)(void *), void *arg, const char *name, int prio);
void yield(void);
void thread_exit(void) __attribute__((noreturn));
int  thread_self(void);
void thread_sleep_ms(unsigned ms);
void thread_tick(void);
void thread_dump(void);
int  threads_running(void);
void sched_set_mode(int mode);
int  sched_get_mode(void);
const char *sched_mode_name(void);
void sched_run_benchmark(void);
int  proc_exec(const void *img, size_t len, const char *name);
void user_syscall(struct trapframe *tf);
void user_fault(struct trapframe *tf, u64 esr);

/* ---------- virtio / fs / net ---------- */
void virtio_irq(unsigned slot);
int  blk_init(void);
u64  blk_sectors(void);
int  blk_read(u64 sector, unsigned count, void *buf);
int  tarfs_init(void);
int  tarfs_rescan(void);         /* hot-reload the file list (verbose) */
int  tarfs_rescan_quiet(void);   /* hot-reload the file list (silent, auto-called on ls & HTTP) */
struct tar_file { char name[100]; u64 size; u64 offset; };
const struct tar_file *tarfs_find(const char *name);
const struct tar_file *tarfs_get(int i);
int  tarfs_count(void);
int  tarfs_read(const struct tar_file *f, u64 off, void *buf, size_t len);
int  net_init(void);
void net_thread(void *arg);
void net_get_ip(char *buf, size_t n);
void httpd_init(void);

void shell_thread(void *arg);

#endif
