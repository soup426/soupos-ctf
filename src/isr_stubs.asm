; isr_stubs.asm — ISR and IRQ entry stubs
; Each stub saves state, calls the C handler, then restores and returns.

extern isr_handler
extern irq_handler
extern syscall_dispatch

; Common ISR stub: state is already on stack (int_no + err_code pushed by macro)
isr_common:
    pusha                   ; push eax, ecx, edx, ebx, esp, ebp, esi, edi
    mov ax, ds              ; save the interrupted context's data selector
    push eax
    mov ax, 0x10            ; kernel data segment for the handler
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp                ; pass registers_t * as argument (cdecl)
    call isr_handler
    add esp, 4              ; clean up argument
    pop eax                 ; restore the caller's data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8              ; pop int_no and err_code
    iret

; Common IRQ stub
irq_common:
    pusha
    mov ax, ds              ; save the interrupted context's data selector
    push eax
    mov ax, 0x10            ; kernel data segment for the handler
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp                ; pass registers_t * as argument (cdecl)
    call irq_handler
    add esp, 4              ; clean up argument
    pop eax                 ; restore the caller's data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8
    iret

; Syscall entry — int 0x80. Same register frame as an ISR (dummy err code +
; int_no) so syscall_dispatch can use registers_t. The handler writes its
; result back into the saved eax, which popa then restores for the caller.
; iret correctly returns to ring 3 or ring 0 depending on the saved CS.
global isr128
isr128:
    push dword 0
    push dword 128
    pusha
    mov ax, ds              ; save the interrupted context's data selector
    push eax
    mov ax, 0x10            ; kernel data segment for the handler
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call syscall_dispatch
    add esp, 4
    pop eax                 ; restore the caller's data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8
    iret

; Macro for exceptions WITHOUT an error code (CPU doesn't push one)
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push dword 0            ; dummy error code
    push dword %1           ; interrupt number
    jmp isr_common
%endmacro

; Macro for exceptions WITH an error code (CPU already pushed it)
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push dword %1           ; interrupt number (error code already on stack)
    jmp isr_common
%endmacro

; Macro for hardware IRQs (no error code; remap offset = 32)
%macro IRQ 2
global irq%1
irq%1:
    push dword 0
    push dword %2
    jmp irq_common
%endmacro

; CPU exceptions 0-31
ISR_NOERRCODE  0   ; Divide by zero
ISR_NOERRCODE  1   ; Debug
ISR_NOERRCODE  2   ; NMI
ISR_NOERRCODE  3   ; Breakpoint
ISR_NOERRCODE  4   ; Overflow
ISR_NOERRCODE  5   ; Bound range exceeded
ISR_NOERRCODE  6   ; Invalid opcode
ISR_NOERRCODE  7   ; Device not available
ISR_ERRCODE    8   ; Double fault        (has error code)
ISR_NOERRCODE  9   ; Coprocessor segment overrun
ISR_ERRCODE   10   ; Invalid TSS         (has error code)
ISR_ERRCODE   11   ; Segment not present (has error code)
ISR_ERRCODE   12   ; Stack-segment fault (has error code)
ISR_ERRCODE   13   ; General protection  (has error code)
ISR_ERRCODE   14   ; Page fault          (has error code)
ISR_NOERRCODE 15   ; Reserved
ISR_NOERRCODE 16   ; x87 FPU error
ISR_ERRCODE   17   ; Alignment check     (has error code)
ISR_NOERRCODE 18   ; Machine check
ISR_NOERRCODE 19   ; SIMD floating point
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

; Hardware IRQs 0-15 → vectors 32-47
IRQ  0, 32
IRQ  1, 33
IRQ  2, 34
IRQ  3, 35
IRQ  4, 36
IRQ  5, 37
IRQ  6, 38
IRQ  7, 39
IRQ  8, 40
IRQ  9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47
