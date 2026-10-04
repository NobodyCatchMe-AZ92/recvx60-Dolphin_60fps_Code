SECTIONS
{
  . = 0x817B7000;
  .text : { *(.text) *(.text.*) }
  /DISCARD/ : { *(.eh_frame) }
  .rodata : { *(.rodata) *(.rodata.*) *(.sdata2) *(.sdata2.*) }
  .data : { *(.data) *(.data.*) *(.sdata) *(.sdata.*) }
  __blob_end = .;
  . = ALIGN(32);
  __bss_start = .;
  .bss : { *(.bss) *(.bss.*) *(.sbss) *(.sbss.*) *(COMMON) }
  __bss_end = .;
}
