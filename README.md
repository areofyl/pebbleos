# PebbleOS

a minimal aarch64 kernel written from scratch in C and ARM64 assembly, targeting QEMU's virt machine. no libc, no dependencies — everything from scratch.

## what it does right now

- boots on QEMU virt (cortex-a72), drops from EL2 to EL1
- prints to UART (PL011 at `0x09000000`)
- exception vector table with sync exception handler
- timer interrupt via GIC + ARM generic timer (1 tick/second)

## build and run

```bash
make          # build kernel.bin
make run      # boot in QEMU
make clean    # clean up
```

you need `gcc`, `ld`, `objcopy`, and `qemu-system-aarch64`.

## files

| file | what it does |
|------|-------------|
| `boot.S` | entry point — EL2 drop, stack setup, vector install, jump to main |
| `vectors.S` | exception vector table (16 entries, macro-generated) |
| `main.c` | uart driver + main, sets up gic/timer and unmasks irqs |
| `exception.c` | sync exception handler — prints ESR/ELR/FAR and halts |
| `gic.c` | GICv2 setup — distributor, cpu interface, enables IRQ 30 |
| `timer.c` | ARM generic timer — 1 second countdown, tick counter |
| `irq.c` | IRQ handler — dispatches on interrupt ID, handles timer ticks |
| `linker.ld` | memory layout starting at `0x40080000` |
| `Makefile` | build system |

## roadmap

- [x] UART hello world
- [x] exception vectors
- [x] timer interrupt
- [ ] physical memory allocator
- [ ] MMU / virtual memory
- [ ] scheduler
- [ ] user space (EL0) + syscalls
- [ ] filesystem (ramdisk)
- [ ] shell
