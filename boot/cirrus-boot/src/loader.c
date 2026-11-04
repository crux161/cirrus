#include "cirrus_boot.h"
#include "cirrus_bootinfo.h"

#define PL011_BASE 0x09000000u
#define KERNEL_PHYS 0x48000000u
#define KERNEL_VIRT 0xffff800048000000ull
#define KERNEL_LEN 0x00200000ull
#define QEMU_RAM_BASE 0x40000000ull
#define QEMU_RAM_SIZE 0x40000000ull
#define STUB_ENTRY 0xffff800048000000ull

#define DESC_TABLE 0x3ull
#define DESC_BLOCK (0x1ull | (3ull << 8) | (1ull << 10) | (1ull << 54))

static struct cirrus_boot_plan plan;
static struct cirrus_bootinfo_mem_region regions[CIRRUS_BOOTINFO_MAX_REGIONS];
static struct cirrus_bootinfo bootinfo;
static uint64_t *cirrus_ttbr0_root;


static volatile uint32_t *const uart = (volatile uint32_t *)PL011_BASE;

static void uart_init(void)
{
    uart[0] = 0;
    uart[0x24 / 4] = 13;   /* IBRD */
    uart[0x28 / 4] = 0;    /* FBRD */
    uart[0x2c / 4] = 0x70; /* 8N1, FIFO */
    uart[0x30 / 4] = 0x301;/* UART, TX, RX */
}

static void uart_putc(char c)
{
    while ((uart[0x18 / 4] & (1u << 5)) != 0) {
    }
    uart[0] = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *text)
{
    while (*text != '\0') {
        uart_putc(*text++);
    }
}

static void uart_put_u32(uint32_t value)
{
    char digits[10];
    uint32_t count = 0;
    if (value == 0) {
        uart_putc('0');
        return;
    }
    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count != 0) {
        uart_putc(digits[--count]);
    }
}

static void fail(const char *message)
{
    uart_puts("CIRRUS loader: FAIL ");
    uart_puts(message);
    uart_puts("\n");
    for (;;) {
        __asm__ volatile("wfi");
    }
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void zero_words(uint64_t *words, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        words[i] = 0;
    }
}

static uint64_t block_desc(uint64_t phys, uint32_t attr_index)
{
    return (phys & ~0x1fffffull) | DESC_BLOCK | ((uint64_t)attr_index << 2);
}

static uint64_t build_page_tables(void)
{
    static uint64_t ttbr0_l0[512] __attribute__((aligned(4096)));
    static uint64_t ttbr0_l1[512] __attribute__((aligned(4096)));
    static uint64_t ttbr1_l0[512] __attribute__((aligned(4096)));
    static uint64_t ttbr1_l1[512] __attribute__((aligned(4096)));
    static uint64_t ttbr1_l2[512] __attribute__((aligned(4096)));

    zero_words(ttbr0_l0, 512);
    zero_words(ttbr0_l1, 512);
    cirrus_ttbr0_root = ttbr0_l0;
    zero_words(ttbr1_l0, 512);
    zero_words(ttbr1_l1, 512);
    zero_words(ttbr1_l2, 512);

    ttbr0_l0[0] = (uint64_t)(uintptr_t)ttbr0_l1 | DESC_TABLE;
    /* First 1 GiB: Device-nGnRnE for MMIO including the QEMU UART. */
    ttbr0_l1[0] = block_desc(0x00000000ull, 1u);
    /* Second 1 GiB: Normal WB for RAM, loader, DTB and BootInfo. */
    ttbr0_l1[1] = block_desc(0x40000000ull, 0u);

    const uint64_t va = KERNEL_VIRT;
    uint32_t i0 = (uint32_t)((va >> 39) & 0x1ffu);
    uint32_t i1 = (uint32_t)((va >> 30) & 0x1ffu);
    uint32_t i2 = (uint32_t)((va >> 21) & 0x1ffu);
    ttbr1_l0[i0] = (uint64_t)(uintptr_t)ttbr1_l1 | DESC_TABLE;
    ttbr1_l1[i1] = (uint64_t)(uintptr_t)ttbr1_l2 | DESC_TABLE;
    ttbr1_l2[i2] = block_desc(KERNEL_PHYS, 0u);
    return (uint64_t)(uintptr_t)ttbr1_l0;
}

static void enter_high(uint64_t entry, uint64_t ttbr1, const void *boot)
{
    const uint64_t ttbr0_root = (uint64_t)(uintptr_t)cirrus_ttbr0_root;

    const uint64_t tcr = 16ull | (16ull << 16) |
                         (1ull << 8) | (1ull << 10) | (3ull << 12) |
                         (1ull << 24) | (1ull << 26) | (3ull << 28) |
                         (2ull << 30) | (2ull << 32);
    const uint64_t mair = 0xffull; /* attr0 Normal WB, attr1 Device */
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ull << 0) | (1ull << 2) | (1ull << 12);

    __asm__ volatile(
        "dsb sy\n"
        "msr ttbr0_el1, %0\n"
        "msr ttbr1_el1, %1\n"
        "msr tcr_el1, %2\n"
        "msr mair_el1, %3\n"
        "dsb sy\n"
        "tlbi vmalle1\n"
        "dsb sy\n"
        "isb\n"
        "msr sctlr_el1, %4\n"
        "isb\n"
        "mov x0, %5\n"
        "br %6\n"
        :: "r"(ttbr0_root), "r"(ttbr1), "r"(tcr), "r"(mair),
           "r"(sctlr), "r"(boot), "r"(entry)
        : "x0", "memory");
    __builtin_unreachable();
}

void cirrus_loader_main(const void *dtb)
{
    uart_init();
    uart_puts("CIRRUS loader: entry\n");
    if (dtb == NULL) {
#ifdef CIRRUS_LOADER_TEST
        const uint8_t *test_dtb = (const uint8_t *)(uintptr_t)0x47000000ull;
        if (be32(test_dtb) == 0xd00dfeedu) {
            dtb = test_dtb;
        }
#endif
    }
    if (dtb == NULL) {
        fail("no DTB");
    }
    const uint8_t *fdt = (const uint8_t *)dtb;
    if (be32(fdt) != 0xd00dfeedu) {
        fail("bad DTB magic");
    }
    uint32_t dtb_len = be32(fdt + 4);
    int result = cirrus_boot_plan_from_fdt_with_defaults(
        dtb, dtb_len, (uint64_t)(uintptr_t)dtb, QEMU_RAM_BASE,
        QEMU_RAM_SIZE, &plan);
    if (result != CIRRUS_BOOT_OK) {
        fail(cirrus_boot_error_string(result));
    }
    uart_puts("CIRRUS loader: board=");
    uart_puts(plan.board_id);
    uart_puts(" mem=");
    uart_put_u32((uint32_t)plan.usable_count);
    uart_puts(" reserved=");
    uart_put_u32((uint32_t)plan.reserved_count);
    uart_puts("\n");

    struct cirrus_bootinfo_inputs inputs;
    for (size_t i = 0; i < sizeof(inputs); i++) {
        ((uint8_t *)&inputs)[i] = 0;
    }
    inputs.kernel_phys_start = KERNEL_PHYS;
    inputs.kernel_phys_len = KERNEL_LEN;
    inputs.kernel_virt_start = KERNEL_VIRT;
    inputs.kernel_virt_len = KERNEL_LEN;
    size_t region_count = 0;
    result = cirrus_bootinfo_build(&plan, &inputs, regions,
                                   CIRRUS_BOOTINFO_MAX_REGIONS,
                                   &region_count, &bootinfo);
    if (result != CIRRUS_BOOT_OK) {
        fail(cirrus_boot_error_string(result));
    }
    uart_puts("CIRRUS loader: bootinfo regions=");
    uart_put_u32((uint32_t)region_count);
    uart_puts("\n");

    uint64_t ttbr1 = build_page_tables();
    uart_puts("CIRRUS loader: entering high half\n");
    enter_high(STUB_ENTRY, ttbr1, &bootinfo);
}
