ENTRY(_start)

SECTIONS
{
  . = 0x40080000;

  .text : ALIGN(0x1000) {
    KEEP(*(.text._start))
    *(.text .text.*)
  }

  .rodata : ALIGN(0x1000) {
    *(.rodata .rodata.*)
  }

  .data : ALIGN(0x1000) {
    *(.data .data.*)
  }

  .bss : ALIGN(0x1000) {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(16);
    __bss_end = .;
  }

  /* High-virtual test payload, physically loaded at 0x48000000. */
  . = 0xffff800048000000;
  .stub : AT(0x48000000) ALIGN(0x1000) {
    KEEP(*(.stub.text))
    *(.stub.rodata)
  }

  /DISCARD/ : {
    *(.comment)
    *(.note .note.*)
    *(.eh_frame .eh_frame_hdr)
  }
}
