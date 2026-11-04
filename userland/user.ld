/* CIRRUS first-userland link layout. USER_IMAGE_BASE is 0x200000 in the
 * kernel's aarch64 bootstrap plan. */
ENTRY(_start)

SECTIONS
{
  . = 0x200000;

  .text ALIGN(0x1000) : {
    KEEP(*(.text._start))
    *(.text .text.*)
  }

  .rodata ALIGN(0x1000) : {
    *(.rodata .rodata.*)
    *(.srodata .srodata.*)
  }

  .data ALIGN(0x1000) : {
    *(.data .data.*)
    *(.sdata .sdata.*)
  }

  .bss ALIGN(0x1000) (NOLOAD) : {
    __bss_start = .;
    *(.bss .bss.*)
    *(.sbss .sbss.*)
    *(COMMON)
    . = ALIGN(16);
    __bss_end = .;
  }

  /DISCARD/ : {
    *(.comment)
    *(.note .note.*)
    *(.eh_frame .eh_frame_hdr)
  }
}
