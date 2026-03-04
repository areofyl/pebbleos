/*
 * exception vector table — 16 entries, 4 groups of 4.
 * each entry is 0x80 bytes apart, whole table aligned to 2048.
 *
 * the macro now takes a handler param so we can send irqs
 * to irq_handler and everything else to exception_handler.
 */

.section .text

.macro VECTOR_ENTRY label handler
.balign 0x80
\label:
    stp x0, x1, [sp, #-16]!
    stp x2, x3, [sp, #-16]!
    stp x4, x5, [sp, #-16]!
    mrs x0, esr_el1
    mrs x1, elr_el1
    mrs x2, far_el1
    bl \handler
    ldp x4, x5, [sp], #16
    ldp x2, x3, [sp], #16
    ldp x0, x1, [sp], #16
    eret
.endm

.balign 2048
.global vectors
vectors:
  /* current EL, SP_EL0 — we don't use this mode but gotta fill it */
  VECTOR_ENTRY vec_cur_sp0_sync,   exception_handler
  VECTOR_ENTRY vec_cur_sp0_irq,    irq_handler
  VECTOR_ENTRY vec_cur_sp0_fiq,    exception_handler
  VECTOR_ENTRY vec_cur_sp0_serror, exception_handler

  /* current EL, SP_ELx — this is us, kernel mode */
  VECTOR_ENTRY vec_cur_spx_sync,   exception_handler
  VECTOR_ENTRY vec_cur_spx_irq,    irq_handler       /* timer irqs land here */
  VECTOR_ENTRY vec_cur_spx_fiq,    exception_handler
  VECTOR_ENTRY vec_cur_spx_serror, exception_handler

  /* lower EL, aarch64 — user space stuff, for later */
  VECTOR_ENTRY vec_lower64_sync,   exception_handler
  VECTOR_ENTRY vec_lower64_irq,    irq_handler
  VECTOR_ENTRY vec_lower64_fiq,    exception_handler
  VECTOR_ENTRY vec_lower64_serror, exception_handler

  /* lower EL, aarch32 — not using this */
  VECTOR_ENTRY vec_lower32_sync,   exception_handler
  VECTOR_ENTRY vec_lower32_irq,    irq_handler
  VECTOR_ENTRY vec_lower32_fiq,    exception_handler
  VECTOR_ENTRY vec_lower32_serror, exception_handler
