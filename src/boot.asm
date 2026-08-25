; boot.asm — Multiboot entry point
; GRUB scans the first 8KB of the kernel ELF for the Multiboot header.

MBOOT_MAGIC     equ 0x1BADB002
MBOOT_FLAGS     equ 0x00000003   ; bit 0: align modules on 4K, bit 1: give us memory map
MBOOT_CHECKSUM  equ -(MBOOT_MAGIC + MBOOT_FLAGS)

STACK_SIZE      equ 0x8000       ; 32 KB stack

section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM

section .bss
align 16
stack_bottom:
    resb STACK_SIZE
stack_top:

section .text
global _start
extern kernel_main

_start:
    ; Set up stack — stack grows downward so we point esp at the top
    mov esp, stack_top

    ; Push Multiboot info pointer (ebx) and magic (eax) as args to kernel_main
    push ebx
    push eax

    call kernel_main

    ; If kernel_main ever returns, hang forever
.hang:
    cli
    hlt
    jmp .hang
