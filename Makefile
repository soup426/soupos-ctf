TARGET  = kernel.elf
ISO     = soupOS.iso
DISK    = disk.img

CC      = gcc
# -mgeneral-regs-only keeps GCC out of the SSE/MMX/x87 register files. Without
# it, GCC 12+ auto-vectorises ordinary byte loops at -O2 (e.g. zeroing the TSS
# in gdt_init) into `pxor xmm0,xmm0` and friends. The kernel never enables SSE
# (no CR4.OSFXSR / FXSAVE area), and QEMU's default `qemu32` CPU does not even
# expose SSE2 — so the first such instruction is an invalid opcode, which with
# no IDT loaded yet triple-faults the machine before the GDT is installed.
CFLAGS  = -m32 -std=gnu99 -ffreestanding -fno-stack-protector -fno-builtin \
          -nostdlib -Wall -Wextra -Iinclude -Isrc \
          -mgeneral-regs-only \
          -O2 -g -fno-omit-frame-pointer

AS      = nasm
ASFLAGS = -f elf32

LD      = ld
LDFLAGS = -m elf_i386 -T linker.ld --oformat elf32-i386

SRC_C   = $(wildcard src/*.c)
SRC_ASM = $(wildcard src/*.asm)

OBJ_C   = $(SRC_C:.c=.o)
OBJ_ASM = $(SRC_ASM:.asm=.o)
OBJS    = $(OBJ_C) $(OBJ_ASM)

# Ring-3 user programs (freestanding, linked high at USER_BASE; see user/user.ld).
# Each links the user runtime (user/ulib.c) which provides _start + syscalls.
UCFLAGS  = -m32 -ffreestanding -fno-pie -no-pie -fno-stack-protector \
           -nostdlib -Isrc -mgeneral-regs-only -O2
USER_ELFS = user/hello.elf user/cat.elf user/echo.elf user/systest.elf user/spin.elf

.PHONY: all clean distclean run run-ai iso disk user

all: iso

# Build all user-mode ELF programs
user: $(USER_ELFS)
user/%.elf: user/%.c user/ulib.c user/ulib.h user/user.ld src/syscall_nr.h
	$(CC) $(UCFLAGS) -Wl,-T,user/user.ld -o $@ user/ulib.c user/$*.c

# Compile C
src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble
src/%.o: src/%.asm
	$(AS) $(ASFLAGS) $< -o $@

# Link (libgcc provides __divdi3/__udivdi3 for 64-bit integer math on 32-bit targets)
$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(shell $(CC) -m32 -print-libgcc-file-name)

# Build ISO
iso: $(TARGET)
	cp $(TARGET) iso/boot/kernel.elf
	grub-mkrescue -o $(ISO) iso 2>/dev/null

# Create FAT16 disk image with test files (32 MB, primary master)
$(DISK):
	dd if=/dev/zero of=$(DISK) bs=1M count=32 2>/dev/null
	mkfs.fat -F 16 -n "SOUPOS" $(DISK) >/dev/null
	printf 'Hello from soupOS!\nA warm bowl of kernel soup.\n' \
	    | mcopy -i $(DISK) - ::HELLO.TXT
	printf 'soupOS v0.7.4\nA 32-bit hobby OS built in C and NASM.\nFeatures: GDT IDT PMM Paging Heap FAT16 bowls users VFS scheduler preempt ring3 syscalls ELF soupyc jot ai\n' \
	    | mcopy -i $(DISK) - ::README.TXT
	printf 'Ingredients:\n  - C source code\n  - NASM stubs\n  - A linker script\n  - love\n' \
	    | mcopy -i $(DISK) - ::RECIPE.TXT
	printf 'The secret ingredient is soup.\n' \
	    | mcopy -i $(DISK) - ::SECRET.TXT
	printf '# soupyc demo - run with: soup DEMO.SC\nlet i = 1\npour "Counting to 10:"\nwhile i <= 10 {\n    pour i\n    i = i + 1\n}\npour ""\npour "What is your name?"\nlet name = input\npour "Hello, " + name + "!"\npour ""\nlet x = 6\nlet y = 7\npour "Is " + x + " * " + y + " = 42? " + (x * y == 42)\n' \
	    | mcopy -i $(DISK) - ::DEMO.SC
	printf '# Functions demo - run with: soup FN.SC\nfn factorial(n) {\n    if n <= 1 {\n        return 1\n    }\n    return n * factorial(n - 1)\n}\nfn fib(n) {\n    if n <= 1 { return n }\n    return fib(n - 1) + fib(n - 2)\n}\npour "Factorials:"\nlet i = 1\nwhile i <= 8 {\n    pour i + "! = " + factorial(i)\n    i = i + 1\n}\npour ""\npour "Fibonacci:"\ni = 0\nwhile i < 10 {\n    pour "fib(" + i + ") = " + fib(i)\n    i = i + 1\n}\npour ""\npour "hash(soupOS) = " + hash("soupOS")\n' \
	    | mcopy -i $(DISK) - ::FN.SC
	printf '# Beep scale demo - run with: soup BEEP.SC\npour "Playing C major scale..."\nbeep 262 200\nbeep 294 200\nbeep 330 200\nbeep 349 200\nbeep 392 200\nbeep 440 200\nbeep 494 200\nbeep 523 300\npour "Done!"\n' \
	    | mcopy -i $(DISK) - ::BEEP.SC
	printf '# soupyc stdlib demo - run with: soup STDLIB.SC\npour "abs(-42)    = " + abs(-42)\npour "min(7, 3)   = " + min(7, 3)\npour "max(7, 3)   = " + max(7, 3)\npour "len(soupOS) = " + len("soupOS")\npour "upper(soup) = " + upper("soup")\npour "lower(LOUD) = " + lower("LOUD")\npour "substr      = " + substr("soupOS rocks", 0, 6)\npour "chr(65)     = " + chr(65)\npour "ord(Z)      = " + ord("Z")\npour "int(456)    = " + int("456")\npour "uptime tick = " + time()\npour "rand 0..32k = " + rand()\n' \
	    | mcopy -i $(DISK) - ::STDLIB.SC
	printf '# soupyc array demo - run with: soup ARRAY.SC\nlet nums = [4, 8, 15, 16, 23]\npour "nums      ="\npour nums\npour "len(nums) = " + len(nums)\npour "nums[2]   = " + nums[2]\nnums[2] = 99\npush(nums, 42)\npour "after nums[2]=99 and push(42):"\npour nums\npour "pop()     = " + pop(nums)\nlet squares = []\nfor k in 1..7 {\n    push(squares, k * k)\n}\npour "squares 1..6 ="\npour squares\n' \
	    | mcopy -i $(DISK) - ::ARRAY.SC
	printf '# soupyc file I/O demo - run with: soup FILEIO.SC\nlet f = open("NOTE.TXT", "w")\nwrite(f, "soup is best served hot")\nclose(f)\npour "wrote NOTE.TXT"\nlet g = open("NOTE.TXT")\npour "read back: " + read(g, 40)\nclose(g)\nlet log = open("/dev/serial", "w")\nwrite(log, "FILEIO.SC logged this on the serial port\\n")\nclose(log)\npour "logged a line to /dev/serial"\n' \
	    | mcopy -i $(DISK) - ::FILEIO.SC
	printf '# a tiny library - included by INCLUDE.SC\nfn cube(n) { return n * n * n }\n' \
	    | mcopy -i $(DISK) - ::MATHLIB.SC
	printf '# include + new builtins demo - run with: soup INCLUDE.SC\ninclude "MATHLIB.SC"\npour "cube(3)  = " + cube(3)\nlet nums = [5, 2, 9, 1, 7]\nsort(nums)\npour "sorted   ="\npour nums\npour "sum      = " + sum(nums)\nreverse(nums)\npour "reversed ="\npour nums\npour "find OS  = " + find("soupOS", "OS")\n' \
	    | mcopy -i $(DISK) - ::INCLUDE.SC
	$(MAKE) user
	mcopy -i $(DISK) -o user/hello.elf   ::HELLO.ELF
	mcopy -i $(DISK) -o user/cat.elf     ::CAT.ELF
	mcopy -i $(DISK) -o user/echo.elf    ::ECHO.ELF
	mcopy -i $(DISK) -o user/systest.elf ::SYSTEST.ELF
	mcopy -i $(DISK) -o user/spin.elf    ::SPIN.ELF
	@if [ -f DOOM1.WAD ]; then \
	    echo "  Copying DOOM1.WAD to disk image..."; \
	    mcopy -i $(DISK) DOOM1.WAD ::DOOM1.WAD; \
	else \
	    echo "  (DOOM1.WAD not found — skipping; copy it to $(CURDIR) to enable)"; \
	fi

disk: $(DISK)

# Copy DOOM1.WAD into an existing disk image (safe to run on a live disk.img)
wad: $(DISK)
	@if [ -f DOOM1.WAD ]; then \
	    echo "Copying DOOM1.WAD → disk.img..."; \
	    mcopy -i $(DISK) -o DOOM1.WAD ::DOOM1.WAD && echo "Done."; \
	else \
	    echo "DOOM1.WAD not found in $(CURDIR)."; \
	    echo "Copy the shareware WAD here and run: make wad"; \
	fi

# Run with VGA window (interactive — click the window to type, Ctrl+Alt+G to release)
# Primary master (index=0) = disk.img   Secondary master (index=2) = cdrom
run: iso $(DISK)
	qemu-system-i386 \
	    -drive file=$(DISK),format=raw,if=ide,index=0 \
	    -cdrom $(ISO) -boot d \
	    -m 128M -no-reboot -no-shutdown \
	    -display gtk -serial file:/tmp/soupos-serial.log

# Run with the `ai` serial bridge attached on COM2. COM1 still goes to the log
# file; COM2 is a unix socket the host daemon connects to. Replies come from a
# local Ollama by default — set AI_FAKE=1 for offline canned replies. The
# daemon is started in the background and killed when QEMU exits.
run-ai: iso $(DISK)
	@rm -f /tmp/soupos-ai.sock
	@echo "soupOS + AI bridge.  (AI_FAKE=1 for offline replies; else needs Ollama)"
	@python3 tools/ai_bridge.py /tmp/soupos-ai.sock & echo $$! > /tmp/soupos-ai.pid; \
	qemu-system-i386 \
	    -drive file=$(DISK),format=raw,if=ide,index=0 \
	    -cdrom $(ISO) -boot d \
	    -m 128M -no-reboot -no-shutdown \
	    -display gtk \
	    -serial file:/tmp/soupos-serial.log \
	    -serial unix:/tmp/soupos-ai.sock,server,nowait; \
	kill `cat /tmp/soupos-ai.pid` 2>/dev/null; rm -f /tmp/soupos-ai.pid

clean:
	rm -f src/*.o $(TARGET) $(ISO) iso/boot/kernel.elf user/*.elf

# Also removes the disk image (user data)
distclean: clean
	rm -f $(DISK)
