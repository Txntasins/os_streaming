#include "kernel.h"

extern char vectors[];
extern char __kernel_end[];

void kmain(void) {
    mmu_init();                      /* Enable MMU + cache (identity map) */
    kprintf("\n=== OS Streaming Kernel (AArch64) ===\n");
    kprintf("[boot] MMU on, kernel image ends at %p\n", __kernel_end);

    pmm_init();
    kheap_init();
    kprintf("[mm]   %lu MB free for pages, kernel heap %lu KB\n",
            (unsigned long)(pmm_free_pages() * PAGE_SIZE >> 20), 4096UL);

#ifdef __aarch64__
    __asm__ volatile("msr vbar_el1, %0\nisb" :: "r"(vectors));
#endif
    gic_init();
    thread_init();
    timer_init(100);                 /* 100 Hz = base tick 10 ms */
    kprintf("[sched] %s\n", sched_mode_name());

    blk_init();
    tarfs_init();
    net_init();

    thread_create(net_thread, 0, "net");
    thread_create(shell_thread, 0, "shell");

#ifdef __aarch64__
    __asm__ volatile("msr daifclr, #2");        /* Enable IRQs */
    for (;;) __asm__ volatile("wfi");           /* thread 0 = idle */
#endif
}
