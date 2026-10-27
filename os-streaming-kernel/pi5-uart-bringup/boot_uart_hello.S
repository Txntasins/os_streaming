/*
 * Raspberry Pi 5 (BCM2712) bare-metal bring-up: step 0 — prove UART output works.
 * NOT YET TESTED ON REAL HARDWARE (this sandbox has no Pi 5; only assembled/linked here).
 *
 * RP1's UART0 physical address (0x1f_0003_0000) is taken from the rpi-hal crate's
 * hand-written BCM2712 register map (RP1_BASE=0x1f_0000_0000 + 0x0003_0000), which
 * cites Raspberry Pi's own bcm2712.dtsi/rp1.dtsi as its source:
 *   https://docs.rs/crate/rpi-hal/latest/source/src/pac_bcm2712.rs
 * It is the same PL011 IP as every earlier Pi (and the same IP QEMU's "virt" machine
 * emulates), so the DR/FR register offsets below are identical to the ones already
 * used in drivers/uart.c for QEMU — only the base address differs.
 *
 * CRITICAL: RP1 sits behind a PCIe link. If config.txt leaves "pciex4_reset=1"
 * (the firmware default), the firmware resets RP1 before handing control to this
 * code, and the very first access below silently hangs the core — no crash, no
 * output, nothing. See pi5/config.txt in this folder; it must be used as-is.
 */
.section ".text.boot"
.global _start
_start:
    mrs   x0, mpidr_el1
    and   x0, x0, #0xff
    cbnz  x0, park                     // only core 0 continues; cores 1-3 park

    mrs   x0, CurrentEL
    lsr   x0, x0, #2
    cmp   x0, #2
    b.ne  el1_entry

    // ---- EL2 -> EL1 (Pi firmware with arm_64bit=1 enters the kernel at EL2) ----
    mrs   x0, cnthctl_el2
    orr   x0, x0, #3
    msr   cnthctl_el2, x0
    msr   cntvoff_el2, xzr
    mov   x0, #(1 << 31)               // HCR_EL2.RW = EL1 is AArch64
    msr   hcr_el2, x0
    ldr   x0, =0x30d00800              // SCTLR_EL1 reset value (MMU off)
    msr   sctlr_el1, x0
    mov   x0, #0x3c5                   // SPSR: EL1h, all interrupts masked
    msr   spsr_el2, x0
    adr   x0, el1_entry
    msr   elr_el2, x0
    eret

el1_entry:
    ldr   x0, =stack_top
    mov   sp, x0

    ldr   x19, =0x1f0030000            // RP1 UART0 base (PL011)
loop:
    adr   x20, msg
1:  ldrb  w1, [x20], #1
    cbz   w1, delay
    // wait while TX FIFO full (FR.TXFF, bit 5, at offset 0x18)
2:  ldr   w2, [x19, #0x18]
    tbnz  w2, #5, 2b
    str   w1, [x19]                    // DR at offset 0x00
    b     1b
delay:
    mov   x21, #0x2000000
3:  subs  x21, x21, #1
    b.ne  3b
    b     loop

park:
    wfe
    b     park

.section ".rodata"
msg:
    .asciz "Hello from the Pi 5 (RP1 UART0)!\r\n"

.section ".bss"
.balign 16
stack_bottom:
    .skip 4096
stack_top:
