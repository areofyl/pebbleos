ENTRY(_start)

SECTIONS
{
  . = 0x80000;

  .text : {
    *(.text._start)
    *(.text*)
    
  }

  .rodata : {
    *(.rodata*)
  }

  . = ALIGN(4096);
  _rodata_end = .;

  .data : {
    *(.data*)
  }

  .bss : {
    __bss_start = .;
    *(.bss*)
    __bss_end = .;
  }

  . = ALIGN(16);
  . = . + 4096;
  _stack_top = .;         /* core 0 */

  . = . + 4096;
  _stack_top_core1 = .;   /* core 1 */
  . = . + 4096;
  _stack_top_core2 = .;   /* core 2 */
  . = . + 4096;
  _stack_top_core3 = .;   /* core 3 */

  /* everything after this is free ram for the allocator */
  . = ALIGN(4096);
  _kernel_end = .;
}
