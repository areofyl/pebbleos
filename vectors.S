// vector table - 16 entries, 0x80 apart, aligned to 2048
// macro takes a handler param now so irqs go to irq_handler

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
  // current EL, SP_EL0 (not used but need to fill)
  VECTOR_ENTRY vec_cur_sp0_sync,   exception_handler
  VECTOR_ENTRY vec_cur_sp0_irq,    irq_handler
  VECTOR_ENTRY vec_cur_sp0_fiq,    exception_handler
  VECTOR_ENTRY vec_cur_sp0_serror, exception_handler

  // current EL, SP_ELx (this is us)
  VECTOR_ENTRY vec_cur_spx_sync,   exception_handler
  VECTOR_ENTRY vec_cur_spx_irq,    irq_handler
  VECTOR_ENTRY vec_cur_spx_fiq,    exception_handler
  VECTOR_ENTRY vec_cur_spx_serror, exception_handler

  // lower EL aarch64 (userspace later)
  VECTOR_ENTRY vec_lower64_sync,   exception_handler
  VECTOR_ENTRY vec_lower64_irq,    irq_handler
  VECTOR_ENTRY vec_lower64_fiq,    exception_handler
  VECTOR_ENTRY vec_lower64_serror, exception_handler

  // lower EL aarch32 (not used)
  VECTOR_ENTRY vec_lower32_sync,   exception_handler
  VECTOR_ENTRY vec_lower32_irq,    irq_handler
  VECTOR_ENTRY vec_lower32_fiq,    exception_handler
  VECTOR_ENTRY vec_lower32_serror, exception_handler
