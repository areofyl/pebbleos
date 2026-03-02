.section .text

.macro VECTOR_ENTRY label
.balign 0x80
\label:
    stp x0, x1, [sp, #-16]!
    stp x2, x3, [sp, #-16]!
    stp x4, x5, [sp, #-16]!
    mrs x0, esr_el1
    mrs x1, elr_el1
    mrs x2, far_el1
    bl exception_handler
    ldp x4, x5, [sp], #16
    ldp x2, x3, [sp], #16
    ldp x0, x1, [sp], #16
    eret
.endm

.balign 2048
.global vectors
vectors:
  VECTOR_ENTRY vec_cur_sp0_sync
  VECTOR_ENTRY vec_cur_sp0_irq
  VECTOR_ENTRY vec_cur_sp0_fiq
  VECTOR_ENTRY vec_cur_sp0_serror
  VECTOR_ENTRY vec_cur_spx_sync
  VECTOR_ENTRY vec_cur_spx_irq
  VECTOR_ENTRY vec_cur_spx_fiq
  VECTOR_ENTRY vec_cur_spx_serror
  VECTOR_ENTRY vec_lower64_sync
  VECTOR_ENTRY vec_lower64_irq
  VECTOR_ENTRY vec_lower64_fiq
  VECTOR_ENTRY vec_lower64_serror
  VECTOR_ENTRY vec_lower32_sync
  VECTOR_ENTRY vec_lower32_irq
  VECTOR_ENTRY vec_lower32_fiq
  VECTOR_ENTRY vec_lower32_serror
