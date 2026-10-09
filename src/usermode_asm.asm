; usermode.asm — ring 0 <-> ring 3 transitions.
;
; user_mode_enter() saves the kernel's callee-saved state, records the esp to
; come back to ON THE CURRENT TASK (which is also the esp0 the CPU loads when
; this task traps from ring 3), then iret's into ring 3 at `entry` with
; `user_stack_top` as ESP. The process runs until it makes the exit syscall,
; faults, or is killed; each of those reaches user_mode_exit(), which restores
; that task's kernel stack and returns out of user_mode_enter as if it had just
; returned normally.
;
; The resume point used to be a single word in .bss, which is the reason only
; one ring-3 program could be alive at a time. It lives on the task now, so
; each process unwinds into its own exec_elf.

global user_mode_enter
global user_mode_exit
extern usermode_arm_resume
extern usermode_resume_esp

section .text

; void user_mode_enter(uint32_t entry, uint32_t user_stack_top)
user_mode_enter:
    push ebp
    push ebx
    push esi
    push edi

    ; Remember where to come back to. `push esp` pushes the value ESP had
    ; BEFORE the push, which is exactly what user_mode_exit must restore:
    ; just above these four saved registers, so a trap frame pushed from
    ; ring 3 lands below them instead of on top of them.
    push esp
    call usermode_arm_resume         ; record on the task + load tss.esp0
    add esp, 4

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

; void user_mode_exit(void)  — invoked (in ring 0) from the exit syscall, the
; ring-3 fault handler, or the kill unwind. Never returns to its caller;
; unwinds back into user_mode_enter's caller on THIS task.
user_mode_exit:
    cli
    call usermode_resume_esp         ; eax = this task's resume esp
    mov esp, eax
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
