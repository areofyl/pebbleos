#include <stdint.h>

volatile uint8_t *uart = (volatile uint8_t *)0x09000000;

void putchar(char c) { *uart = c; }

void print(const char *s) {
  while (*s) {
    putchar(*s);
    s++;
  }
}

void main() { print("Hello from PebbleOS"); }
