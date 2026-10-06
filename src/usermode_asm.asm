; usermode.asm — ring 0 <-> ring 3 transitions.
;
; user_mode_enter() saves the kernel's callee-saved state, then iret's into
; ring 3 at `entry` with `user_stack_top` as ESP. The process runs until it
; makes the exit syscall, whose handler calls user_mode_exit(), which restores
; the saved kernel stack and returns out of user_mode_enter as if it had just
; returned normally. This is a one-shot coroutine swap (no per-process kernel
; threads yet).

global user_mode_enter
global user_mode_exit

section .bss
kernel_resume_esp: resd 1

section .text

; void user_mode_enter(uint32_t entry, uint32_t user_stack_top)
user_mode_enter:
    push ebp
    push ebx
    push esi
    push edi
    mov [kernel_resume_esp], esp     ; remember where to come back to

    mov eax, [esp + 20]              ; entry        (16 saved + 4 ret)
    mov ebx, [esp + 24]              ; user_stack_top

    cli                              ; build the iret frame atomically
    mov cx, 0x23                     ; user data selector (RPL 3)
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx

    push 0x23                        ; SS  = user data
    push ebx                         ; ESP = user stack top
    pushf
    pop edx
    or  edx, 0x200                   ; set IF so user runs with interrupts on
    push edx                         ; EFLAGS
    push 0x1B                        ; CS  = user code (RPL 3)
    push eax                         ; EIP = entry
    iret                             ; -> ring 3

; void user_mode_exit(void)  — invoked (in ring 0) from the exit syscall.
; Never returns to its caller; unwinds back into user_mode_enter's caller.
user_mode_exit:
    cli
    mov esp, [kernel_resume_esp]
    mov cx, 0x10                     ; restore kernel data segments
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx
    pop edi
    pop esi
    pop ebx
    pop ebp
    sti
    ret
