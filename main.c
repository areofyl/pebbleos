#include <stdint.h>

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

void main() {
  print("Hello from PebbleOS\n");
}
