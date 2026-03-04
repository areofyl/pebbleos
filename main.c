#include <stdint.h>

/* pl011 uart at 0x09000000 on qemu virt */
volatile uint8_t *uart = (volatile uint8_t *)0x09000000;

void putchar(char c) { *uart = c; }

void print(const char *s) {
  while (*s) {
    putchar(*s);
    s++;
  }
}

void print_hex(unsigned long val) {
  char buf[17];
  char hex[] = "0123456789abcdef";
  buf[16] = '\0';
  for (int i = 15; i >= 0; i--) {
    buf[i] = hex[val & 0xf];
    val >>= 4;
  }
  print("0x");
  print(buf);
}

void gic_init(void);
void timer_init(void);

void main() {
  print("Hello from PebbleOS\n\n");

  print("=== Setting up interrupts ===\n");

  /* set up the gic (interrupt controller) so the cpu can hear hardware */
  gic_init();
  print("\n");

  /* set up the timer to fire every 1 second */
  timer_init();
  print("\n");

  /*
   * unmask irqs on the cpu. right now the I bit in DAIF is set (from boot),
   * meaning the cpu is blocking all irqs even though everything else is ready.
   *
   * daifclr #2 clears bit 1 (the I bit). after this, irqs get through.
   */
  asm volatile("msr daifclr, #2");
  print("[cpu] IRQs unmasked -- waiting for timer...\n\n");

  /* just sit here. when a timer irq fires the cpu jumps to the vector table,
   * runs irq_handler(), then comes back here. wfe = low power sleep until
   * the next interrupt so we don't burn host cpu */
  for (;;) {
    asm volatile("wfe");
  }
}
