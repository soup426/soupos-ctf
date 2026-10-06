; gdt_flush.asm — loads the GDT register and reloads segment registers
; Called from C as: void gdt_flush(uint32_t gdt_ptr_addr)

global gdt_flush

section .text
gdt_flush:
    mov eax, [esp+4]    ; get the pointer to the gdt_ptr struct
    lgdt [eax]          ; load it into the GDTR

    ; Reload data segment registers with the kernel data selector (index 2 = 0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Far jump to reload CS with kernel code selector (index 1 = 0x08)
    jmp 0x08:.flush
.flush:
    ret
