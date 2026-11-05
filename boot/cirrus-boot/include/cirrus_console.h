/*
 * CIRRUS loader console: a PL011 sink (QEMU virt) and a polled GENI/QUP sink
 * (SDM845 uart9 @ 0xa84000). The GENI path assumes the boot firmware has
 * already enabled the SE clock and baud; it programs only framing/packing and
 * then runs the polled TX command sequence.
 */
#ifndef CIRRUS_CONSOLE_H
#define CIRRUS_CONSOLE_H

#include <stdint.h>
#include <stddef.h>

#include "cirrus_boot.h"

#ifdef __cplusplus
extern "C" {
#endif

enum cirrus_console_kind {
    CIRRUS_CONSOLE_NONE = 0,
    CIRRUS_CONSOLE_PL011 = 1,
    CIRRUS_CONSOLE_GENI = 2
};

struct cirrus_console {
    uint32_t kind;
    uintptr_t base;
};

void cirrus_console_inert(struct cirrus_console *console);
int cirrus_console_init(struct cirrus_console *console, uint32_t kind,
                        uintptr_t base);
void cirrus_console_putc(struct cirrus_console *console, char ch);
void cirrus_console_puts(struct cirrus_console *console, const char *text);
void cirrus_console_put_u32(struct cirrus_console *console, uint32_t value);
void cirrus_console_put_hex64(struct cirrus_console *console, uint64_t value);

/* GENI polled transmitter, exposed for host tests with a mock MMIO array. */
int cirrus_geni_probe(uintptr_t base);
void cirrus_geni_init(uintptr_t base);
void cirrus_geni_putc(uintptr_t base, char ch);

#ifdef __cplusplus
}
#endif

#endif /* CIRRUS_CONSOLE_H */
