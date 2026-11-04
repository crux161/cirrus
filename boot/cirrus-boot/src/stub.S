// High-half payload used by the QEMU loader-entry test.
// It runs after TTBR1 is enabled and proves that the high virtual mapping
// and branch work. Real KUMO will replace this payload.

.section .stub.text, "ax"
.global cirrus_loader_stub
.type cirrus_loader_stub, %function

cirrus_loader_stub:
    ldr  x1, =0x09000000          // QEMU virt PL011 UART
    adr  x2, stub_message
1:
    ldrb w3, [x2], #1
    cbz  w3, 2f
3:
    ldr  w4, [x1, #0x18]          // UARTFR
    tbnz w4, #5, 3b               // wait while TXFF
    str  w3, [x1]                 // UARTDR
    b    1b
2:
    wfi
    b    2b
.size cirrus_loader_stub, .-cirrus_loader_stub

.section .stub.rodata, "a"
stub_message:
    .asciz "CIRRUS loader entry OK\n"
