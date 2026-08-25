/* Link soupOS user programs at USER_BASE (0xC0000000), well above the kernel's
 * identity-mapped low RAM. exec_elf maps fresh user pages here. */
ENTRY(_start)

SECTIONS
{
    . = 0xC0000000;

    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }

    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
