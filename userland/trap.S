// AAPCS64 C entry for C++/Objective-C++ callers of the public raw trap ABI.
// The two-word result is returned in x0/x1. C callers use the inline header.
.section .text, "ax"
.global kumo_trap
.type kumo_trap, %function
kumo_trap:
    mov x8, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov x3, x4
    mov x4, x5
    mov x5, x6
    svc #0
    ret
.size kumo_trap, .-kumo_trap
.section .note.GNU-stack, "", %progbits
