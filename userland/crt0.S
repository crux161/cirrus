// CIRRUS freestanding C entry point for KUMO userland.
//
// The kernel enters EL0 at _start with x0 holding the bootstrap channel
// handle. The kernel already mapped the image and a user stack; this
// runtime still owns the C invariants: a zeroed .bss and a known SP.

.section .text._start, "ax"
.global _start
.type _start, %function

_start:
    // Zero .bss in 8-byte stores. Symbols come from user.ld.
    adrp x1, __bss_start
    add  x1, x1, :lo12:__bss_start
    adrp x2, __bss_end
    add  x2, x2, :lo12:__bss_end
    mov  x3, #0
1:
    cmp  x1, x2
    b.hs 2f
    str  x3, [x1], #8
    b    1b
2:
    // Use the runtime's own static stack. The kernel-provided stack is left
    // intact for future argv/environment work.
    adrp x1, __stack_top
    add  x1, x1, :lo12:__stack_top
    mov  sp, x1

    bl   main

    // main returned: exit cleanly through the MUREX trap.
    mov  x0, #0
    bl   kumo_process_exit
3:
    b 3b
.size _start, .-_start

.section .bss
.balign 16
.space 16384
.global __stack_top
__stack_top:

.section .note.GNU-stack, "", %progbits
