CC = gcc
AS = gcc
LD = ld
OBJCOPY = objcopy

CFLAGS = -ffreestanding -nostdlib -O2 -Wall
ASFLAGS = -c

all: kernel.bin

boot.o: boot.S
	$(AS) $(ASFLAGS) boot.S -o boot.o

main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o main.o

kernel.elf: boot.o main.o linker.ld
	$(LD) -T linker.ld boot.o main.o -o kernel.elf

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary kernel.elf kernel.bin

run: kernel.bin
	qemu-system-aarch64 -M virt -cpu cortex-a72 -nographic -kernel kernel.bin

clean:
	rm -f *.o kernel.elf kernel.bin
