.section .text._start
.global _start

_start:
  // RPi 5 has 4 cores — park cores 1-3
  mrs x0, mpidr_el1
  and x0, x0, #3
  cbz x0, primary_core
park:
  wfe
  b park

primary_core:
  // check if we're at EL2
  mrs x0, CurrentEL
  lsr x0, x0, #2
  cmp x0, #2
  b.ne at_el1

  // drop to EL1
  mov x0, #(1 << 31)
  msr hcr_el2, x0

  // allow EL1 access to physical timer registers
  mov x0, #3
  msr cnthctl_el2, x0
  msr cntvoff_el2, xzr

  mov x0, #0x3c5
  msr spsr_el2, x0
  adr x0, at_el1
  msr elr_el2, x0
  eret

at_el1:
  ldr x0, =_stack_top
  mov sp, x0

  // enable fp/simd otherwise the compiler generated code traps
  mov x0, #(3 << 20)
  msr cpacr_el1, x0
  isb

  // zero bss (qemu does this for us but real hw wouldnt)
  ldr x0, =__bss_start
  ldr x1, =__bss_end
bss_loop:
  cmp x0, x1
  b.ge bss_done
  str xzr, [x0], #8
  b bss_loop
bss_done:

  // set up vector table
  ldr x0, =vectors
  msr vbar_el1, x0
  isb

  bl main

halt:
  wfe
  b halt

// ---- secondary core entry (called by PSCI CPU_ON) ----
// firmware jumps here after primary calls psci_cpu_on()
// each core needs its own stack, system registers, and MMU setup
// but they all share the same page tables that core 0 already built

.global secondary_entry
secondary_entry:
  // figure out which core we are (0-3)
  mrs x19, mpidr_el1
  and x19, x19, #3

  // drop from EL2 to EL1 (same as primary core)
  mrs x0, CurrentEL
  lsr x0, x0, #2
  cmp x0, #2
  b.ne .sec_el1

  mov x0, #(1 << 31)
  msr hcr_el2, x0
  mov x0, #3
  msr cnthctl_el2, x0
  msr cntvoff_el2, xzr
  mov x0, #0x3c5
  msr spsr_el2, x0
  adr x0, .sec_el1
  msr elr_el2, x0
  eret

.sec_el1:
  // per-core stack: _stack_top_core1 + (core_id - 1) * 4096
  ldr x0, =_stack_top_core1
  sub x1, x19, #1
  lsl x1, x1, #12
  add x0, x0, x1
  mov sp, x0

  // enable fp/simd (compiler needs it)
  mov x0, #(3 << 20)
  msr cpacr_el1, x0
  isb

  // same vector table as primary
  ldr x0, =vectors
  msr vbar_el1, x0
  isb

  // call C — core ID as argument
  mov x0, x19
  bl secondary_main

.sec_halt:
  wfe
  b .sec_halt
