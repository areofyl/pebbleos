.section .text._start
.global _start

_start:
  ldr x0, =_stack_top
  mov sp, x0
  bl main

halt:
  wfe
  b halt
