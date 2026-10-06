/* linker.ld — soupOS kernel linker script
   Load the kernel at 1MB (0x100000), standard for Multiboot kernels. */

ENTRY(_start)

SECTIONS
{
    . = 0x00100000;

    /* Multiboot header must be in the first output section */
    .text ALIGN(4K) :
    {
        *(.multiboot)
        *(.text)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata)
    }

    .data ALIGN(4K) :
    {
        *(.data)
    }

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss)
        . = ALIGN(4K);
        kernel_end = .;
    }
}
