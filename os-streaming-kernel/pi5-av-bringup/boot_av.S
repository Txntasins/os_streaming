/*
 * Raspberry Pi 5 (BCM2712 Cortex-A76) Bare-Metal Boot Entry for
 * Complete HDMI Framebuffer + MJPEG Video + Audio (PWM0 + I2S0 + vc4-hdmi) + Kernel Timer A/V Sync
 */
.section ".text.boot"
.global _start

_start:
    /* Save Device Tree Blob (DTB) pointer passed by Pi 5 firmware in x0 */
    mov     x19, x0

    /* Park secondary cores (only core 0 runs bare-metal AV player) */
    mrs     x1, mpidr_el1
    and     x1, x1, #3
    cbnz    x1, .Lhalt

    /* Enable FP/SIMD (CPACR_EL1.FPEN = 0b11) */
    mov     x1, #(3 << 20)
    msr     cpacr_el1, x1
    isb

    /* Set up stack below load address 0x80000 */
    ldr     x1, =_start
    mov     sp, x1

    /* Clear .bss */
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
.Lbss_loop:
    cmp     x1, x2
    b.hs    .Lbss_done
    str     xzr, [x1], #8
    b       .Lbss_loop
.Lbss_done:

    mov     x0, x19
    bl      pi5_av_main

.Lhalt:
    wfe
    b       .Lhalt
