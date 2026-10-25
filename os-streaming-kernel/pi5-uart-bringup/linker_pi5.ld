ENTRY(_start)
SECTIONS {
    /* Must match kernel_address= in config.txt */
    . = 0x40080000;
    .text   : { *(.text.boot) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    . = ALIGN(8);
    __bss_start = .;
    .bss    : { *(.bss*) *(COMMON) }
    . = ALIGN(8);
    __bss_end = .;
}
