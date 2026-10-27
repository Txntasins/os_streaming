/*
 * Raspberry Pi 5 (BCM2712) Bare-Metal Bring-Up — Step 1: HDMI Framebuffer + RP1 UART0
 *
 * Preserves x0 (DTB physical address passed by Pi 5 firmware) in x19,
 * drops from EL2 to EL1 (AArch64), sets up a 16 KB stack, and jumps to fb_main(dtb_ptr).
 */
.section ".text.boot"
.global _start
_start:
    mov   x19, x0                      // Save DTB pointer passed by Pi 5 firmware in x0

    mrs   x0, mpidr_el1
    and   x0, x0, #0xff
    cbnz  x0, park                     // Only core 0 continues; cores 1-3 park

    mrs   x0, CurrentEL
    lsr   x0, x0, #2
    cmp   x0, #2
    b.ne  el1_entry

    // ---- EL2 -> EL1 drop ----
    mrs   x0, cnthctl_el2
    orr   x0, x0, #3
    msr   cnthctl_el2, x0
    msr   cntvoff_el2, xzr
    mov   x0, #(1 << 31)               // HCR_EL2.RW = EL1 is AArch64
    msr   hcr_el2, x0
    ldr   x0, =0x30d00800              // SCTLR_EL1 reset value (MMU off)
    msr   sctlr_el1, x0
    mov   x0, #0x3c5                   // SPSR: EL1h, interrupts masked
    msr   spsr_el2, x0
    adr   x0, el1_entry
    msr   elr_el2, x0
    eret

el1_entry:
    ldr   x0, =stack_top
    mov   sp, x0

    // Clear BSS
    ldr   x1, =__bss_start
    ldr   x2, =__bss_end
1:  cmp   x1, x2
    b.hs  2f
    str   xzr, [x1], #8
    b     1b

2:  mov   x0, x19                      // Pass saved DTB pointer to fb_main(u64 dtb_ptr)
    bl    fb_main

park:
    wfe
    b     park

.section ".bss"
.balign 16
stack_bottom:
    .skip 16384
stack_top:
