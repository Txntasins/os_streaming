/* Kernel threads + Preemptive Priority / MLFQ & Round-Robin Scheduler */
#include "kernel.h"
#include <string.h>

#define MAX_THREADS  16
#define KSTACK_PAGES 4                    /* 16 KB per thread */

enum { T_FREE, T_READY, T_SLEEP, T_DEAD };

struct context {                          /* The order must match arch/switch.S */
    u64 x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    u64 fp, lr, sp;
};

struct thread {
    struct context ctx;
    int   state;
    int   prio;                           /* MLFQ queue level: 0 (highest) .. 3 (lowest) */
    int   base_prio;                      /* Initial priority when created */
    u32   slice_ticks;                    /* Ticks used in current time quantum */
    u32   demotions;                      /* Number of times demoted for exhausting quantum */
    u64   wake_ms;
    u64  *ttbr0;                          /* 0 = use kernel page table */
    void *kstack;
    int   user_slot;                      /* -1 = not a user process */
    u64   run_ticks;
    char  name[16];
};

extern void cpu_switch(struct context *prev, struct context *next);
extern void thread_trampoline(void);

static struct thread threads[MAX_THREADS];
static int current;
static u64 *cur_table;
static u8 slot_used[USER_SLOTS];

static int sched_mode = SCHED_MLFQ;       /* Default to upgraded MLFQ scheduler */
static u64 total_ticks;
static u64 boost_count;
static u64 ctx_switches;

/* Time quantum (in 10ms ticks) per MLFQ level: Q0=1 (10ms), Q1=2 (20ms), Q2=4 (40ms), Q3=8 (80ms) */
static inline u32 quantum_for_prio(int prio) {
    if (sched_mode == SCHED_RR) return 1;
    if (prio < 0) prio = 0;
    if (prio >= MLFQ_LEVELS) prio = MLFQ_LEVELS - 1;
    return 1U << prio;
}

void sched_set_mode(int mode) {
    unsigned long f = irq_save();
    sched_mode = (mode == SCHED_RR) ? SCHED_RR : SCHED_MLFQ;
    for (int i = 1; i < MAX_THREADS; i++) {
        threads[i].prio = threads[i].base_prio;
        threads[i].slice_ticks = 0;
    }
    irq_restore(f);
}

int sched_get_mode(void) { return sched_mode; }

const char *sched_mode_name(void) {
    return (sched_mode == SCHED_MLFQ)
        ? "preemptive MLFQ (4 queues: Q0=10ms..Q3=80ms, boost 2s)"
        : "preemptive round-robin, 10 ms slice";
}

void thread_init(void) {
    memset(threads, 0, sizeof(threads));
    threads[0].state = T_READY;           /* thread 0 = kmain, acts as idle */
    threads[0].user_slot = -1;
    threads[0].prio = MLFQ_LEVELS - 1;
    threads[0].base_prio = MLFQ_LEVELS - 1;
    strncpy(threads[0].name, "idle", 15);
    cur_table = mmu_kernel_table();
}

static void reap(struct thread *t) {      /* Reclaim resources of an exited thread */
    if (t->ttbr0) {
        u64 l2 = t->ttbr0[(USER_VBASE >> 30) & 511] & ~0xfffUL;
        pmm_free((void *)l2, 1);
        pmm_free(t->ttbr0, 1);
        t->ttbr0 = 0;
    }
    if (t->user_slot >= 0) { slot_used[t->user_slot] = 0; t->user_slot = -1; }
}

static int create(void (*fn)(void *), void *arg, const char *name, u64 *table, int slot, int prio) {
    unsigned long f = irq_save();
    int id = -1;
    for (int i = 1; i < MAX_THREADS; i++)
        if (threads[i].state == T_FREE || threads[i].state == T_DEAD) { id = i; break; }
    if (id < 0) { irq_restore(f); return -1; }
    struct thread *t = &threads[id];
    if (t->state == T_DEAD) reap(t);
    if (!t->kstack) t->kstack = pmm_alloc(KSTACK_PAGES);
    if (!t->kstack) { irq_restore(f); return -1; }
    memset(&t->ctx, 0, sizeof(t->ctx));
    t->ctx.x19 = (u64)fn;
    t->ctx.x20 = (u64)arg;
    t->ctx.lr  = (u64)thread_trampoline;
    t->ctx.sp  = (u64)t->kstack + KSTACK_PAGES * PAGE_SIZE;
    t->ttbr0 = table; t->user_slot = slot; t->run_ticks = 0; t->wake_ms = 0;
    if (prio < 0) prio = 0;
    if (prio >= MLFQ_LEVELS) prio = MLFQ_LEVELS - 1;
    t->prio = prio;
    t->base_prio = prio;
    t->slice_ticks = 0;
    t->demotions = 0;
    strncpy(t->name, name, 15); t->name[15] = 0;
    t->state = T_READY;
    irq_restore(f);
    return id;
}

int thread_create(void (*fn)(void *), void *arg, const char *name) {
    return create(fn, arg, name, 0, -1, 0);
}

int thread_create_prio(void (*fn)(void *), void *arg, const char *name, int prio) {
    return create(fn, arg, name, 0, -1, prio);
}

int thread_self(void) { return current; }

/* Can be called both from a thread (voluntarily yielding CPU) and from a timer IRQ (preemption) */
void yield(void) {
    unsigned long f = irq_save();
    u64 now = uptime_ms();
    int prev = current, next = 0;

    /* 1. Wake up any sleeping threads whose timer has expired */
    for (int i = 1; i < MAX_THREADS; i++) {
        struct thread *t = &threads[i];
        if (t->state == T_SLEEP && now >= t->wake_ms) {
            t->state = T_READY;
            t->slice_ticks = 0;
        }
    }

    if (sched_mode == SCHED_RR) {
        /* Classic Round-Robin: scan circularly from prev + 1 */
        for (int k = 1; k <= MAX_THREADS; k++) {
            int i = (prev + k) % MAX_THREADS;
            if (i == 0) continue;
            if (threads[i].state == T_READY) { next = i; break; }
        }
        if (next == 0 && prev != 0 && threads[prev].state == T_READY) next = prev;
    } else {
        /* MLFQ Priority Scheduler:
           Find the highest priority level (0 = highest .. 3 = lowest) that has a T_READY thread,
           and round-robin among threads within that priority level. */
        int best_prio = MLFQ_LEVELS;
        for (int i = 1; i < MAX_THREADS; i++) {
            if (threads[i].state == T_READY && threads[i].prio < best_prio)
                best_prio = threads[i].prio;
        }
        if (best_prio < MLFQ_LEVELS) {
            /* If current thread is still T_READY at best_prio and hasn't finished its quantum,
               let it continue its multi-tick quantum unless it yielded voluntarily */
            if (prev != 0 && threads[prev].state == T_READY &&
                threads[prev].prio == best_prio &&
                threads[prev].slice_ticks > 0 &&
                threads[prev].slice_ticks < quantum_for_prio(best_prio)) {
                next = prev;
            } else {
                for (int k = 1; k <= MAX_THREADS; k++) {
                    int i = (prev + k) % MAX_THREADS;
                    if (i == 0) continue;
                    if (threads[i].state == T_READY && threads[i].prio == best_prio) {
                        next = i;
                        break;
                    }
                }
            }
        }
    }

    if (next != prev) {
        struct thread *n = &threads[next];
        u64 *tbl = n->ttbr0 ? n->ttbr0 : mmu_kernel_table();
        if (tbl != cur_table) { mmu_switch(tbl); cur_table = tbl; }
        current = next;
        ctx_switches++;
        cpu_switch(&threads[prev].ctx, &n->ctx);
    }
    irq_restore(f);
}

void thread_sleep_ms(unsigned ms) {
    unsigned long f = irq_save();
    threads[current].wake_ms = uptime_ms() + ms;
    threads[current].state = T_SLEEP;
    threads[current].slice_ticks = 0;     /* Voluntary I/O sleep resets slice so interactive threads stay high prio */
    irq_restore(f);
    yield();
}

void thread_exit(void) {
    unsigned long f = irq_save();
    threads[current].state = T_DEAD;
    irq_restore(f);
    yield();
    for (;;) {
#ifdef __aarch64__
        __asm__ volatile("wfe");
#endif
    }
}

void thread_tick(void) {
    struct thread *t = &threads[current];
    t->run_ticks++;
    t->slice_ticks++;
    total_ticks++;

    if (sched_mode == SCHED_MLFQ) {
        /* Rule 4: If thread used its entire time quantum at this level, demote it */
        if (current != 0 && t->state == T_READY) {
            u32 q = quantum_for_prio(t->prio);
            if (t->slice_ticks >= q) {
                t->slice_ticks = 0;
                if (t->prio < MLFQ_LEVELS - 1) {
                    t->prio++;
                    t->demotions++;
                }
            }
        }
        /* Rule 5: Periodic anti-starvation priority boost every MLFQ_BOOST_TICKS (2.0s) */
        if (total_ticks % MLFQ_BOOST_TICKS == 0) {
            for (int i = 1; i < MAX_THREADS; i++) {
                if (threads[i].state != T_FREE && threads[i].state != T_DEAD) {
                    threads[i].prio = 0;
                    threads[i].slice_ticks = 0;
                }
            }
            boost_count++;
        }
    } else {
        t->slice_ticks = 0;
    }
}

int threads_running(void) {
    int n = 0;
    for (int i = 0; i < MAX_THREADS; i++) n += (threads[i].state == T_READY || threads[i].state == T_SLEEP);
    return n;
}

void thread_dump(void) {
    static const char *st[] = { "free", "ready", "sleep", "dead" };
    kprintf("  Scheduler: %s (switches=%lu, boosts=%lu)\n",
            sched_mode_name(), (unsigned long)ctx_switches, (unsigned long)boost_count);
    kprintf("  ID  NAME             STATE   PRIO  DEMOTE  CPU(ticks)  KIND\n");
    for (int i = 0; i < MAX_THREADS; i++) {
        struct thread *t = &threads[i];
        if (t->state == T_FREE) continue;
        kprintf("  %2d  %-16s %-6s  Q%-3d  %6u  %10lu  %s%s\n",
                i, t->name, st[t->state], t->prio, t->demotions, (unsigned long)t->run_ticks,
                t->ttbr0 ? "user process" : "kernel thread", i == current ? " <- running" : "");
    }
}

/* --------- In-Kernel Scheduler Benchmark (RR vs MLFQ for OS Report) --------- */
static volatile int bench_stop;
static volatile u64 bench_work_iters[4];
static volatile u64 bench_io_wakeups;

static void bench_cpu_worker(void *arg) {
    int idx = (int)(u64)arg;
    while (!bench_stop) {
        for (int k = 0; k < 5000; k++) {
            __asm__ volatile("" ::: "memory");
        }
        bench_work_iters[idx]++;
    }
    thread_exit();
}

static void bench_io_worker(void *arg) {
    (void)arg;
    while (!bench_stop) {
        thread_sleep_ms(10);
        bench_io_wakeups++;
    }
    thread_exit();
}

static void bench_yield_peer(void *arg) {
    int *loops = (int *)arg;
    while ((*loops) > 0) {
        (*loops)--;
        yield();
    }
    thread_exit();
}

static inline u64 read_cntvct(void) {
    u64 v = 0;
#ifdef __aarch64__
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
#endif
    return v;
}

static inline u64 read_cntfrq(void) {
    u64 f = 1000000000ULL;
#ifdef __aarch64__
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
#endif
    return f ? f : 1000000000ULL;
}

static void run_mode_test(int mode, u64 *out_ticks_sum, u64 *out_fairness_pct, u64 *out_io_wakes, u32 *out_demotes) {
    sched_set_mode(mode);
    bench_stop = 0;
    bench_io_wakeups = 0;
    for (int i = 0; i < 4; i++) bench_work_iters[i] = 0;

    int pids[4];
    for (int i = 0; i < 4; i++) {
        pids[i] = thread_create(bench_cpu_worker, (void *)(u64)i, "cpu_work");
    }
    int io_pid = thread_create(bench_io_worker, 0, "io_interactive");

    /* Let the 4 CPU-bound threads + 1 I/O interactive thread compete for 500 ms (50 ticks) */
    thread_sleep_ms(500);
    bench_stop = 1;
    thread_sleep_ms(40);

    u64 sum = 0, sum_sq = 0;
    u32 dem = 0;
    for (int i = 0; i < 4; i++) {
        u64 w = bench_work_iters[i] ? bench_work_iters[i] : 1;
        sum += w;
        sum_sq += w * w;
        if (pids[i] >= 0) dem += threads[pids[i]].demotions;
    }
    /* Jain's Fairness Index = (sum)^2 / (N * sum_sq) * 100% */
    *out_ticks_sum = sum;
    *out_fairness_pct = sum_sq ? ((sum * sum * 100ULL) / (4ULL * sum_sq)) : 100ULL;
    *out_io_wakes = bench_io_wakeups;
    *out_demotes = dem;
    (void)io_pid;
}

void sched_run_benchmark(void) {
    int saved_mode = sched_mode;
    kprintf("\n====================================================================\n");
    kprintf(" In-Kernel Scheduler Benchmark: Round-Robin vs MLFQ (AArch64)\n");
    kprintf("====================================================================\n");

    /* 1. Measure raw context-switch time using ARM Generic Timer counter (cntvct_el0) */
    sched_set_mode(SCHED_RR);
    int peer_loops = 500;
    thread_create(bench_yield_peer, &peer_loops, "ctx_peer");
    u64 frq = read_cntfrq();
    u64 t0 = read_cntvct();
    while (peer_loops > 0) {
        yield();
    }
    u64 t1 = read_cntvct();
    u64 total_switches = 1000;
    u64 delta_cycles = (t1 > t0) ? (t1 - t0) : 1000;
    u64 ns_per_switch = (delta_cycles * 1000000000ULL) / (frq * total_switches);
    kprintf(" [1] Context-Switch Overhead (1,000 switches via cpu_switch):\n");
    kprintf("     Timer frequency      : %lu Hz\n", (unsigned long)frq);
    kprintf("     Avg context switch   : %lu ns (%lu timer cycles)\n\n",
            (unsigned long)ns_per_switch, (unsigned long)(delta_cycles / total_switches));

    /* 2. Compare Round-Robin vs MLFQ under mixed 4x CPU-bound + 1x I/O-interactive load (500 ms each) */
    u64 rr_work = 0, rr_fair = 0, rr_io = 0; u32 rr_dem = 0;
    u64 mlfq_work = 0, mlfq_fair = 0, mlfq_io = 0; u32 mlfq_dem = 0;

    kprintf(" [2] Running 500 ms Mixed Workload (4 CPU-bound + 1 I/O thread)...\n");
    run_mode_test(SCHED_RR,   &rr_work,   &rr_fair,   &rr_io,   &rr_dem);
    run_mode_test(SCHED_MLFQ, &mlfq_work, &mlfq_fair, &mlfq_io, &mlfq_dem);

    kprintf("     ---------------------------------------------------------------\n");
    kprintf("     Metric                   Round-Robin (10ms)    MLFQ (Q0..Q3)\n");
    kprintf("     ---------------------------------------------------------------\n");
    kprintf("     Jain Fairness Index      %14lu %%    %11lu %%\n", (unsigned long)rr_fair, (unsigned long)mlfq_fair);
    kprintf("     CPU Work Iterations      %16lu    %13lu\n", (unsigned long)rr_work, (unsigned long)mlfq_work);
    kprintf("     I/O Thread Wakeups       %16lu    %13lu (Q0 priority)\n", (unsigned long)rr_io, (unsigned long)mlfq_io);
    kprintf("     CPU-Hog Demotions        %16u    %13u (Q0->Q3)\n", rr_dem, mlfq_dem);
    kprintf("     ---------------------------------------------------------------\n");
    kprintf("====================================================================\n");

    sched_set_mode(saved_mode);
}

/* --------- user process --------- */
extern void enter_user(u64 entry, u64 sp);
static void user_entry(void *arg) {
    (void)arg;
    enter_user(USER_VBASE, USER_STACK_TOP);
}

int proc_exec(const void *img, size_t len, const char *name) {
    if (len > USER_SLOT_SIZE - 0x10000) return -1;
    unsigned long f = irq_save();
    int slot = -1;
    for (int i = 0; i < USER_SLOTS; i++) if (!slot_used[i]) { slot = i; break; }
    if (slot < 0) { irq_restore(f); return -2; }
    slot_used[slot] = 1;
    irq_restore(f);

    u64 phys = USER_PHYS_BASE + (u64)slot * USER_SLOT_SIZE;
    memset((void *)phys, 0, USER_SLOT_SIZE);
    memcpy((void *)phys, img, len);
#ifdef __aarch64__
    __asm__ volatile("dsb ish\nic iallu\ndsb ish\nisb" ::: "memory");   /* Make the instruction cache see the new code */
#endif
    u64 *table = mmu_new_user_table(phys);
    if (!table) { slot_used[slot] = 0; return -3; }
    int id = create(user_entry, 0, name, table, slot, 0);
    if (id < 0) { slot_used[slot] = 0; return -4; }
    return id;
}
