# telOS

a tiny aarch64 SASOS kernel, written from scratch in C and ARM64 assembly. no libc, no dependencies — everything from `putchar` to the compiler runs on bare metal.

SASOS = Single Address Space OS. all tasks share one set of page tables. instead of swapping page tables on context switch (expensive TLB flush), telOS flips AP permission bits so only the running task can touch its memory. simpler and faster.

runs on QEMU virt (cortex-a72). there's also an [rpi5 branch](https://github.com/areofyl/telos/tree/rpi5) targeting real hardware.

## demo

```
  _       _  ___  ____
 | |_ ___| |/ _ \/ ___|
 | __/ _ \ | | | \___ \
 | ||  __/ | |_| |___) |
  \__\___|_|\___/|____/

Type 'help' for commands.
Try 'cat hello.c' then 'cc hello.c'

telos> cc hello.c
[cc] running...
0
1
2
3
4
5
6
7
8
9

[cc] exit code: 0
telos>
```

## features

**kernel**
- EL2 → EL1 drop on boot, exception vectors, full fault dumps
- GIC + ARM generic timer, preemptive round-robin scheduler
- bitmap page allocator (4KB pages, 128MB)
- MMU with 4KB granule, identity-mapped kernel, per-task VA slots
- SASOS isolation via AP bit flipping + UXN on context switch
- EL0 userspace with syscalls (`svc #0`)
- synchronous IPC (send/recv/call/reply)

**userspace**
- UART server (PL011)
- nameserver — tasks find each other by name, nothing hardcoded
- tag-based filesystem — files have key:value tags instead of directories
- shell (tsh) with ls, cat, create, write, tag, query, ps, top, telfetch
- text editor (teled) — Ctrl+S to save, Ctrl+Q to quit
- C compiler (tcc) — JIT compiles to aarch64 and runs it

## the C compiler

the `cc` command reads a source file, compiles it in a single pass (recursive descent), emits aarch64 machine code into a buffer, flushes icache, and calls it. no assembler, no linker, no ELF — just raw instructions.

```
telos> teled fizzbuzz.c       // write your program
telos> cc fizzbuzz.c          // compile and run
```

**supported:** `int` variables, `if`/`else`, `while`, `return`, arithmetic (`+ - * / %`), comparisons (`< > <= >= == !=`), logical (`&& ||`), `putc()`, `getc()`, character and integer literals, `//` comments.

## the tag-based filesystem

no directories. files have tags:

```
telos> create notes.txt
telos> write notes.txt remember to fix the scheduler
telos> tag notes.txt type text
telos> tag notes.txt topic kernel
telos> query type text
notes.txt
telos> tags notes.txt
  type = text
  topic = kernel
```

## syscalls

| # | name | args | description |
|---|------|------|-------------|
| 0 | write | buf, len | print to UART (len=0 for null-terminated) |
| 1 | yield | | give up time slice |
| 2 | exit | | kill current task |
| 3 | send | pid, buf, len | send message (blocks if target not ready) |
| 4 | recv | buf, max | receive message (blocks until message arrives) |
| 5 | call | pid, buf, len, reply, rmax | send + wait for reply |
| 6 | reply | pid, buf, len | reply to a blocked caller |
| 7 | procinfo | buf, max | get running task info |
| 8 | cacheflush | addr, len | flush dcache + invalidate icache (for JIT) |

## memory map

| range | what |
|-------|------|
| `0x00000000-0x3fffffff` | device memory (UART, GIC, timer) |
| `0x40000000-0x47ffffff` | RAM (128MB, identity mapped) |
| `0x40080000` | kernel load address |
| `0x80000000+` | task slots (16MB each, up to 8) |

## build

```
make          # build kernel.bin
make run      # boot in QEMU
make clean    # clean
```

needs an aarch64 gcc, ld, objcopy, and qemu-system-aarch64.

exit qemu: `ctrl+a` then `x`

## files

| file | what |
|------|------|
| `boot.S` | entry point — EL2 drop, stack, FP/SIMD, BSS zeroing, vectors |
| `vectors.S` | exception vector table, SAVE_ALL/RESTORE_ALL, svc/irq entry |
| `main.c` | userspace: uart server, nameserver, fs server, shell, editor, compiler |
| `exception.c` | fault handler with ESR/ELR/FAR dump |
| `gic.c` | GIC distributor + CPU interface |
| `timer.c` | ARM generic timer (1s periodic tick) |
| `irq.c` | IRQ dispatch, scheduler hook |
| `pmm.c` | bitmap page allocator |
| `mmu.c` | page tables, map/unmap/set_page_flags |
| `proc.c` | task slots, round-robin scheduler, AP bit toggling, IPC |
| `syscall.c` | syscall dispatch |
| `linker.ld` | memory layout (kernel at 0x40080000) |
