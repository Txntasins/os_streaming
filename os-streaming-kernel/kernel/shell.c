/* Small UART shell: type help */
#include "kernel.h"
#include <string.h>

extern char user_hello_start[], user_hello_end[];

static void cmd_help(void) {
    kprintf("  help            show all commands\n");
    kprintf("  uptime          system uptime\n");
    kprintf("  mem             memory status (page allocator + heap)\n");
    kprintf("  ps              list of threads / processes\n");
    kprintf("  ls              files on disk (tar)\n");
    kprintf("  rescan          reload the file list after adding/replacing a video (no reboot needed)\n");
    kprintf("  cat <file>      display file contents (text)\n");
    kprintf("  ifconfig        device IP address\n");
    kprintf("  run             run 1 user program (EL0) (type 'run 3' = 3 programs simultaneously)\n");
    kprintf("  crash           test: make user program fault and see kernel handle it\n");
}

static void cmd_mem(void) {
    kprintf("  page allocator: %lu / %lu pages free (%lu MB free)\n",
            (unsigned long)pmm_free_pages(), (unsigned long)pmm_total_pages(),
            (unsigned long)(pmm_free_pages() * PAGE_SIZE >> 20));
    kprintf("  kernel heap   : %lu bytes in use\n", (unsigned long)kheap_used());
}

static void cmd_ls(void) {
    int n = tarfs_count();
    if (!n) { kprintf("  (No disk or no files)\n"); return; }
    for (int i = 0; i < n; i++) {
        const struct tar_file *f = tarfs_get(i);
        kprintf("  %10lu  %s\n", (unsigned long)f->size, f->name);
    }
}

static void cmd_cat(const char *name) {
    const struct tar_file *f = tarfs_find(name);
    if (!f) { kprintf("  File %s not found\n", name); return; }
    static char buf[512];
    u64 off = 0, lim = f->size < 4096 ? f->size : 4096;
    while (off < lim) {
        size_t n = lim - off < sizeof(buf) ? (size_t)(lim - off) : sizeof(buf);
        if (tarfs_read(f, off, buf, n) < 0) break;
        for (size_t i = 0; i < n; i++) { if (buf[i] == '\n') uart_putc('\r'); uart_putc(buf[i]); }
        off += n;
    }
    kprintf("\n");
}

static void cmd_run(int count) {
    for (int i = 0; i < count; i++) {
        int id = proc_exec(user_hello_start, (size_t)(user_hello_end - user_hello_start), "hello");
        if (id < 0) { kprintf("  Failed to create process (%d)\n", id); return; }
        kprintf("  started user process %d\n", id);
    }
}

static void cmd_crash(void) {
    /* Program writing to a kernel address from user mode: the kernel must kill this process without crashing */
    static const u32 code[] = {
        0xd2a80000,   /* movz x0, #0x4000, lsl #16 -> x0 = 0x40000000 (kernel memory) */
        0xb900001f,   /* str  wzr, [x0]                                               */
        0xd2800048,   /* mov  x8, #2                                                  */
        0xd4000001,   /* svc  #0                                                      */
    };
    int id = proc_exec(code, sizeof(code), "crash");
    kprintf("  started crashing process %d\n", id);
}

static void run_line(char *line) {
    char *arg = line;
    while (*arg && *arg != ' ') arg++;
    if (*arg) { *arg++ = 0; while (*arg == ' ') arg++; }
    if (!*line) return;
    if      (!strcmp(line, "help"))     cmd_help();
    else if (!strcmp(line, "uptime"))   kprintf("  %lu.%03lu s\n", (unsigned long)(uptime_ms() / 1000), (unsigned long)(uptime_ms() % 1000));
    else if (!strcmp(line, "mem"))      cmd_mem();
    else if (!strcmp(line, "ps"))       thread_dump();
    else if (!strcmp(line, "ls"))       cmd_ls();
    else if (!strcmp(line, "cat"))      { if (*arg) cmd_cat(arg); else kprintf("  usage: cat <file>\n"); }
    else if (!strcmp(line, "ifconfig")) { char ip[32]; net_get_ip(ip, sizeof(ip)); kprintf("  ip %s\n", ip); }
    else if (!strcmp(line, "run"))      cmd_run(*arg >= '1' && *arg <= '8' ? *arg - '0' : 1);
    else if (!strcmp(line, "crash"))    cmd_crash();
    else if (!strcmp(line, "rescan"))   { int n = tarfs_rescan(); kprintf("  %d file(s) now visible\n", n < 0 ? 0 : n); }
    else kprintf("  Unknown command '%s' (type help)\n", line);
}

void shell_thread(void *arg) {
    (void)arg;
    char line[128]; int n = 0;
    kprintf("\nType help to see commands\nos> ");
    for (;;) {
        int c = uart_getc();
        if (c < 0) { thread_sleep_ms(20); continue; }
        if (c == '\r' || c == '\n') {
            uart_putc('\r'); uart_putc('\n');
            line[n] = 0; n = 0;
            run_line(line);
            kprintf("os> ");
        } else if ((c == 0x7f || c == 8) && n > 0) {
            n--; kprintf("\b \b");
        } else if (c >= 32 && c < 127 && n < (int)sizeof(line) - 1) {
            line[n++] = (char)c; uart_putc((char)c);
        }
    }
}