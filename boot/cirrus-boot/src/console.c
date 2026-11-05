#include "cirrus_console.h"

/* ---- PL011 (QEMU virt) ------------------------------------------------ */
#define PL011_DR 0x00u
#define PL011_FR 0x18u
#define PL011_IBRD 0x24u
#define PL011_FBRD 0x28u
#define PL011_LCRH 0x2cu
#define PL011_CR 0x30u
#define PL011_FR_TXFF (1u << 5)
#define PL011_CR_ENABLE 0x301u

/* ---- GENI/QUP UART (SDM845 uart9) ------------------------------------- */
#define GENI_STATUS 0x40u
#define GENI_M_CMD0 0x600u
#define GENI_M_CMD_CTRL 0x604u
#define GENI_M_IRQ_STATUS 0x610u
#define GENI_M_IRQ_CLEAR 0x618u
#define GENI_TX_FIFO 0x700u
#define GENI_TX_WATERMARK 0x80cu
#define GENI_TX_TRANS_CFG 0x25cu
#define GENI_TX_PACKING_CFG0 0x260u
#define GENI_TX_PACKING_CFG1 0x264u
#define GENI_TX_WORD_LEN 0x268u
#define GENI_TX_STOP_BIT_LEN 0x26cu
#define GENI_TX_TRANS_LEN 0x270u

#define GENI_M_CMD_ACTIVE (1u << 0)
#define GENI_M_CMD_ABORT (1u << 1)
#define GENI_M_CMD_ABORT_EN (1u << 5)
#define GENI_M_CMD_DONE_EN (1u << 0)
#define GENI_TX_WATERMARK_EN (1u << 30)
#define GENI_START_TX (1u << 27)     /* UART_START_TX << M_OPCODE_SHFT */
#define GENI_TX_CTS_MASK (1u << 1)   /* UART_CTS_MASK */
#define GENI_PACKING_CFG0 0x0000000fu /* 8-bit chars, 4 per FIFO word */
#define GENI_PACKING_CFG1 0x00000000u
#define GENI_TX_WATERMARK_WORDS 2u
#define GENI_POLL_LIMIT 1000000u

static volatile uint32_t *mmio(uintptr_t base, unsigned offset)
{
    return (volatile uint32_t *)(base + offset);
}

static int geni_wait(uintptr_t base, unsigned offset, uint32_t mask,
                     uint32_t value)
{
    for (uint32_t i = 0; i < GENI_POLL_LIMIT; i++) {
        if ((mmio(base, offset)[0] & mask) == value) {
            return 1;
        }
    }
    return 0;
}

static void geni_abort(uintptr_t base)
{
    mmio(base, GENI_M_CMD_CTRL)[0] = GENI_M_CMD_ABORT;
    (void)geni_wait(base, GENI_M_IRQ_STATUS, GENI_M_CMD_ABORT_EN,
                    GENI_M_CMD_ABORT_EN);
    mmio(base, GENI_M_IRQ_CLEAR)[0] = GENI_M_CMD_DONE_EN | GENI_M_CMD_ABORT_EN;
}

int cirrus_geni_probe(uintptr_t base)
{
    if (base == 0u) {
        return 0;
    }
    const uint32_t status = mmio(base, GENI_STATUS)[0];
    return status != 0u && status != 0xffffffffu;
}

void cirrus_geni_init(uintptr_t base)
{
    if (mmio(base, GENI_STATUS)[0] & GENI_M_CMD_ACTIVE) {
        geni_abort(base);
    }
    /* Framing only: the firmware already owns clock, baud and pin mux. */
    mmio(base, GENI_TX_PACKING_CFG0)[0] = GENI_PACKING_CFG0;
    mmio(base, GENI_TX_PACKING_CFG1)[0] = GENI_PACKING_CFG1;
    mmio(base, GENI_TX_TRANS_CFG)[0] = GENI_TX_CTS_MASK;
    mmio(base, GENI_TX_WORD_LEN)[0] = 8u;
    mmio(base, GENI_TX_STOP_BIT_LEN)[0] = 0u;
    mmio(base, GENI_TX_WATERMARK)[0] = GENI_TX_WATERMARK_WORDS;
    mmio(base, GENI_M_IRQ_CLEAR)[0] = GENI_M_CMD_DONE_EN;
}

void cirrus_geni_putc(uintptr_t base, char ch)
{
    mmio(base, GENI_TX_WATERMARK)[0] = GENI_TX_WATERMARK_WORDS;
    mmio(base, GENI_M_IRQ_CLEAR)[0] = GENI_M_CMD_DONE_EN;
    mmio(base, GENI_TX_TRANS_LEN)[0] = 1u;
    mmio(base, GENI_M_CMD0)[0] = GENI_START_TX;
    if (!geni_wait(base, GENI_M_IRQ_STATUS, GENI_TX_WATERMARK_EN,
                   GENI_TX_WATERMARK_EN)) {
        geni_abort(base);
        return;
    }
    mmio(base, GENI_TX_FIFO)[0] = (uint32_t)(uint8_t)ch;
    mmio(base, GENI_M_IRQ_CLEAR)[0] = GENI_TX_WATERMARK_EN;
    if (!geni_wait(base, GENI_M_IRQ_STATUS, GENI_M_CMD_DONE_EN,
                   GENI_M_CMD_DONE_EN)) {
        geni_abort(base);
    }
}

/* ---- Console dispatch ------------------------------------------------ */

void cirrus_console_inert(struct cirrus_console *console)
{
    if (console != NULL) {
        console->kind = CIRRUS_CONSOLE_NONE;
        console->base = 0;
    }
}

static void pl011_init(uintptr_t base)
{
    mmio(base, PL011_IBRD)[0] = 13u;  /* 115200 from a 24 MHz reference */
    mmio(base, PL011_FBRD)[0] = 0u;
    mmio(base, PL011_LCRH)[0] = 0x70u; /* 8N1, FIFOs */
    mmio(base, PL011_CR)[0] = PL011_CR_ENABLE;
}

static void pl011_putc(uintptr_t base, char ch)
{
    while (mmio(base, PL011_FR)[0] & PL011_FR_TXFF) {
    }
    mmio(base, PL011_DR)[0] = (uint32_t)(uint8_t)ch;
}

int cirrus_console_init(struct cirrus_console *console, uint32_t kind,
                        uintptr_t base)
{
    if (console == NULL) {
        return CIRRUS_BOOT_EINVAL;
    }
    cirrus_console_inert(console);
    if (kind == CIRRUS_CONSOLE_PL011) {
        pl011_init(base);
        console->kind = kind;
        console->base = base;
        return CIRRUS_BOOT_OK;
    }
    if (kind == CIRRUS_CONSOLE_GENI) {
        if (!cirrus_geni_probe(base)) {
            return CIRRUS_BOOT_ENODATA;
        }
        cirrus_geni_init(base);
        console->kind = kind;
        console->base = base;
        return CIRRUS_BOOT_OK;
    }
    return CIRRUS_BOOT_EINVAL;
}

void cirrus_console_putc(struct cirrus_console *console, char ch)
{
    if (console == NULL) {
        return;
    }
    if (console->kind == CIRRUS_CONSOLE_PL011) {
        pl011_putc(console->base, ch);
    } else if (console->kind == CIRRUS_CONSOLE_GENI) {
        cirrus_geni_putc(console->base, ch);
    }
}

void cirrus_console_puts(struct cirrus_console *console, const char *text)
{
    while (text != NULL && *text != '\0') {
        cirrus_console_putc(console, *text++);
    }
}

void cirrus_console_put_u32(struct cirrus_console *console, uint32_t value)
{
    char digits[10];
    uint32_t count = 0;
    if (value == 0u) {
        cirrus_console_putc(console, '0');
        return;
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count != 0u) {
        cirrus_console_putc(console, digits[--count]);
    }
}

void cirrus_console_put_hex64(struct cirrus_console *console, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[16];
    for (uint32_t i = 0; i < 16; i++) {
        buffer[15u - i] = digits[value & 0xfu];
        value >>= 4;
    }
    cirrus_console_puts(console, "0x");
    for (uint32_t i = 0; i < 16u; i++) {
        cirrus_console_putc(console, buffer[i]);
    }
}
