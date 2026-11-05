/* Link script for the freestanding CIRRUS loader.
 *
 * LOADER_BASE is supplied by the build: 0x40080000 for the QEMU virt test
 * (-kernel loads the ELF at its PT_LOAD addresses) and 0x80008000 (the
 * Android boot image kernel_addr) for a OnePlus boot image.
 */
ENTRY(_start)

SECTIONS
{
  . = LOADER_BASE;
  __image_start = .;

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

  __image_end = .;

  /DISCARD/ : {
    *(.comment)
    *(.note .note.*)
    *(.eh_frame .eh_frame_hdr)
  }
}
