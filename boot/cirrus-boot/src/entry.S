// CIRRUS bootloader entry.
//
// QEMU/ABL enters with x0 = DTB physical pointer. Install a stack, zero
// .bss, and hand off to C.

.section .text._start, "ax"
.global _start
.type _start, %function

_start:
    adrp x1, __stack_top
    add  x1, x1, :lo12:__stack_top
    mov  sp, x1

    adrp x2, __bss_start
    add  x2, x2, :lo12:__bss_start
    adrp x3, __bss_end
    add  x3, x3, :lo12:__bss_end
    mov  x4, #0
1:
    cmp  x2, x3
    b.hs 2f
    str  x4, [x2], #8
    b    1b
2:
    bl   cirrus_loader_main
3:
    wfi
    b    3b
.size _start, .-_start

.section .bss
.balign 16
.space 32768
.global __stack_top
__stack_top:

.section .note.GNU-stack, "", %progbits
