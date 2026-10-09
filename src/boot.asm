; boot.asm — Multiboot entry point
; GRUB scans the first 8KB of the kernel ELF for the Multiboot header.

MBOOT_MAGIC     equ 0x1BADB002
; bit 0: align modules on 4K, bit 1: memory map, bit 2: video mode request.
;
; Bit 2 is OPT-IN, via `make FB=1`, and that is not a style choice. The video
; fields in this header OVERRIDE grub.cfg's gfxpayload - the opposite of what I
; assumed. With bit 2 set and mode_type 0, GRUB hands over a linear framebuffer
; even when the menu entry says `set gfxpayload=text`, which puts the machine
; in a graphics mode before the kernel runs and leaves the text console with
; nowhere to draw. So the default build does not ask, and the framebuffer is a
; separate image. Choosing at runtime is not possible: this header is read
; before any of our code runs, and leaving a graphics mode for a text one needs
; real-mode BIOS calls that protected mode cannot make.
%ifdef FB_REQUEST
MBOOT_FLAGS     equ 0x00000007
%else
MBOOT_FLAGS     equ 0x00000003
%endif
MBOOT_CHECKSUM  equ -(MBOOT_MAGIC + MBOOT_FLAGS)

STACK_SIZE      equ 0x8000       ; 32 KB stack

section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM
    ; The five a.out fields are not used (flag bit 16 is clear) but the video
    ; fields below sit at fixed offsets, so these have to be here to push them
    ; to offset 32. They are emitted either way and simply ignored when flag
    ; bit 2 is clear.
    dd 0, 0, 0, 0, 0
    dd 0                ; mode_type: 0 = linear graphics, 1 = EGA text
    dd 1024             ; preferred width
    dd 768              ; preferred height
    dd 32               ; preferred depth

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
