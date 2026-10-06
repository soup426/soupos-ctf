; task_switch.asm — cooperative + preemptive context switch primitive
;
; void task_switch(uint32_t *old_esp_out, uint32_t new_esp);
;
; System V cdecl: caller already saved EAX/ECX/EDX.
; We save EFLAGS + the callee-saved regs (EBX/ESI/EDI/EBP) on the current
; stack, store ESP into *old_esp_out, swap to new_esp, restore regs + EFLAGS,
; and ret — which lands in the new task's code (or its trampoline on first run).
;
; EFLAGS is saved so each task's interrupt-enable (IF) travels with it: a task
; preempted inside a timer IRQ (IF=0) and one suspended cooperatively (IF=1)
; both resume with the right IF, which is essential once IRQ0 drives preemption.
;
; A newly-created task's stack is pre-seeded with this same layout:
;   [esp + 0]  = 0 (ebp)
;   [esp + 4]  = 0 (edi)
;   [esp + 8]  = 0 (esi)
;   [esp +12]  = 0 (ebx)
;   [esp +16]  = 0x202 (eflags: IF set, reserved bit 1)
;   [esp +20]  = trampoline   ; ret from task_switch jumps here

global task_switch

section .text
task_switch:
    pushf
    push    ebx
    push    esi
    push    edi
    push    ebp

    ; arg layout after pushf + 4 pushes (each 4 bytes):
    ;   [esp + 0..15]  = saved ebp/edi/esi/ebx
    ;   [esp + 16]     = saved eflags
    ;   [esp + 20]     = return address
    ;   [esp + 24]     = old_esp_out  (first arg)
    ;   [esp + 28]     = new_esp      (second arg)
    mov     eax, [esp + 24]    ; eax = old_esp_out
    mov     edx, [esp + 28]    ; edx = new_esp   (read BEFORE clobbering esp)
    mov     [eax], esp         ; *old_esp_out = current esp
    mov     esp, edx           ; switch stacks

    pop     ebp
    pop     edi
    pop     esi
    pop     ebx
    popf
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
