/*
 * CIRRUS first-light loader.
 *
 * ABL enters at _start with x0 = FDT physical pointer. The loader parses the
 * FDT into a memory plan, finds the KUMO kernel ELF in the KUMORD01 ramdisk
 * that ABL placed (chosen/linux,initrd-start), places the kernel in usable RAM
 * at a 2 MiB-aligned address of its choosing, builds BootInfo, installs TTBR0
 * identity and TTBR1 kernel mappings, and branches to the kernel's linked
 * high-half entry with x0 = BootInfo physical pointer.
 */
#include "cirrus_boot.h"
#include "cirrus_bootinfo.h"
#include "cirrus_console.h"
#include "cirrus_elf.h"
#include "cirrus_initrd.h"

#define KERNEL_ALIGN 0x200000ull
#define DEVICE_1GIB 0x40000000ull
#define TTBR_TABLE 0x3ull
#define TTBR_BLOCK (0x1ull | (3ull << 8) | (1ull << 10) | (1ull << 54))

#ifdef CIRRUS_LOADER_TEST
#define LOADER_CONSOLE_KIND CIRRUS_CONSOLE_PL011
#define LOADER_CONSOLE_BASE 0x09000000u
#define BOARD_RAM_BASE 0x40000000ull
#define BOARD_RAM_SIZE 0x40000000ull
#else
#define LOADER_CONSOLE_KIND CIRRUS_CONSOLE_GENI
#define LOADER_CONSOLE_BASE 0xa84000u
/* Fallback only when the DTB /memory node carries a zero size (observed on
 * the static OnePlus 6 DTB); ABL normally fills the real value. */
#define BOARD_RAM_BASE 0x80000000ull
#define BOARD_RAM_SIZE 0x40000000ull
#endif

extern uint8_t __image_start[];
extern uint8_t __image_end[];

static struct cirrus_boot_plan plan;
static struct cirrus_bootinfo_mem_region regions[CIRRUS_BOOTINFO_MAX_REGIONS];
static struct cirrus_bootinfo bootinfo;
static struct cirrus_console console;
static uint64_t *cirrus_ttbr0_root;

static uint64_t ttbr0_l0[512] __attribute__((aligned(4096)));
static uint64_t ttbr0_l1[512] __attribute__((aligned(4096)));
static uint64_t ttbr1_l0[512] __attribute__((aligned(4096)));
static uint64_t ttbr1_l1[512] __attribute__((aligned(4096)));
static uint64_t ttbr1_l2[512] __attribute__((aligned(4096)));

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void zero_bytes(void *destination, size_t len)
{
    uint8_t *dst = (uint8_t *)destination;
    for (size_t i = 0; i < len; i++) {
        dst[i] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t len)
{
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    for (size_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

static void zero_words(uint64_t *words, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        words[i] = 0;
    }
}

static uint64_t block_desc(uint64_t phys, uint32_t attr_index)
{
    return (phys & ~0x1fffffull) | TTBR_BLOCK | ((uint64_t)attr_index << 2);
}

static void console_hex(const char *label, uint64_t value)
{
    cirrus_console_puts(&console, label);
    cirrus_console_put_hex64(&console, value);
    cirrus_console_putc(&console, '\n');
}

static void fail(const char *message)
{
    cirrus_console_puts(&console, "CIRRUS loader: FAIL ");
    cirrus_console_puts(&console, message);
    cirrus_console_puts(&console, "\n");
    for (;;) {
        __asm__ volatile("wfi");
    }
}

static void loader_copy(void *ctx, uint64_t dest, const void *src, size_t len)
{
    (void)ctx;
    copy_bytes((void *)(uintptr_t)dest, src, len);
}

static void loader_zero(void *ctx, uint64_t dest, size_t len)
{
    (void)ctx;
    zero_bytes((void *)(uintptr_t)dest, len);
}

static int plan_covers(const struct cirrus_boot_plan *p, uint64_t start,
                       uint64_t len)
{
    if (len == 0u || start > ~0ull - len) {
        return 0;
    }
    const uint64_t end = start + len;
    for (size_t i = 0; i < p->usable_count; i++) {
        const struct cirrus_boot_region *r = &p->usable[i];
        if (r->start <= start && end <= r->start + r->len) {
            return 1;
        }
    }
    for (size_t i = 0; i < p->reserved_count; i++) {
        const struct cirrus_boot_region *r = &p->reserved[i];
        if (r->start <= start && end <= r->start + r->len) {
            return 1;
        }
    }
    return 0;
}

static void reserve_or_fail(const char *what, uint64_t start, uint64_t len)
{
    int result = cirrus_boot_plan_reserve(&plan, start, len);
    if (result != CIRRUS_BOOT_OK) {
        cirrus_console_puts(&console, "CIRRUS loader: reserve ");
        cirrus_console_puts(&console, what);
        cirrus_console_puts(&console, ": ");
        cirrus_console_puts(&console, cirrus_boot_error_string(result));
        cirrus_console_puts(&console, "\n");
        fail("usable memory could not be reserved");
    }
}

static void map_normal_range(uint64_t *l1, uint64_t start, uint64_t end)
{
    if (end <= start) {
        return;
    }
    uint64_t block = start & ~(DEVICE_1GIB - 1u);
    for (; block < end; block += DEVICE_1GIB) {
        const size_t index = (size_t)((block >> 30) & 0x1ffu);
        l1[index] = block_desc(block, 0u);
    }
}

static uint64_t build_page_tables(const struct cirrus_elf_image *image,
                                  uint64_t placement, uint64_t loader_start,
                                  uint64_t loader_len)
{
    zero_words(ttbr0_l0, 512);
    zero_words(ttbr0_l1, 512);
    zero_words(ttbr1_l0, 512);
    zero_words(ttbr1_l1, 512);
    zero_words(ttbr1_l2, 512);
    cirrus_ttbr0_root = ttbr0_l0;

    ttbr0_l0[0] = (uint64_t)(uintptr_t)ttbr0_l1 | TTBR_TABLE;
    /* MMIO window: GENI uart9 and the QEMU PL011 both live below 1 GiB. */
    ttbr0_l1[0] = block_desc(0x0ull, 1u);

    for (size_t i = 0; i < plan.usable_count; i++) {
        map_normal_range(ttbr0_l1, plan.usable[i].start,
                         plan.usable[i].start + plan.usable[i].len);
    }
    for (size_t i = 0; i < plan.reserved_count; i++) {
        map_normal_range(ttbr0_l1, plan.reserved[i].start,
                         plan.reserved[i].start + plan.reserved[i].len);
    }
    map_normal_range(ttbr0_l1, loader_start, loader_start + loader_len);
    map_normal_range(ttbr0_l1, plan.dtb_start, plan.dtb_start + plan.dtb_len);
    map_normal_range(ttbr0_l1, plan.initrd_start,
                     plan.initrd_start + plan.initrd_len);
    map_normal_range(ttbr0_l1, placement,
                     placement + (image->load_end - image->load_base));

    const uint64_t span = image->virt_end - image->virt_base;
    const uint64_t va = image->virt_base;
    const uint32_t i0 = (uint32_t)((va >> 39) & 0x1ffu);
    const uint32_t i1 = (uint32_t)((va >> 30) & 0x1ffu);
    const uint32_t i2 = (uint32_t)((va >> 21) & 0x1ffu);
    const uint64_t blocks = (span + KERNEL_ALIGN - 1u) / KERNEL_ALIGN;
    if (blocks == 0u || blocks > 512u - i2) {
        fail("kernel virtual span cannot be mapped");
    }
    ttbr1_l0[i0] = (uint64_t)(uintptr_t)ttbr1_l1 | TTBR_TABLE;
    ttbr1_l1[i1] = (uint64_t)(uintptr_t)ttbr1_l2 | TTBR_TABLE;
    for (uint64_t n = 0; n < blocks; n++) {
        ttbr1_l2[i2 + n] = block_desc(placement + n * KERNEL_ALIGN, 0u);
    }
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
    /* Firmware (ABL/UEFI) hands off with FP/SIMD access enabled. QEMU's
     * -kernel path does not, and the kernel's compiler-generated SIMD traps
     * as an undefined instruction if CPACR_EL1.FPEN stays 0. */
    const uint64_t cpacr = 3ull << 20;
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ull << 0) | (1ull << 2) | (1ull << 12);

    __asm__ volatile(
        "dsb sy\n"
        "msr ttbr0_el1, %0\n"
        "msr ttbr1_el1, %1\n"
        "msr tcr_el1, %2\n"
        "msr mair_el1, %3\n"
        "msr cpacr_el1, %7\n"
        "dsb sy\n"
        "tlbi vmalle1\n"
        "dsb sy\n"
        "isb\n"
        "msr sctlr_el1, %4\n"
        "isb\n"
        "mov x0, %5\n"
        "br %6\n"
        :: "r"(ttbr0_root), "r"(ttbr1), "r"(tcr), "r"(mair),
           "r"(sctlr), "r"(boot), "r"(entry), "r"(cpacr)
        : "x0", "memory");
    __builtin_unreachable();
}

void cirrus_loader_main(const void *dtb)
{
    cirrus_console_inert(&console);
    (void)cirrus_console_init(&console, LOADER_CONSOLE_KIND,
                              LOADER_CONSOLE_BASE);
    cirrus_console_puts(&console, "CIRRUS loader: entry\n");

    if (dtb == NULL) {
#ifdef CIRRUS_LOADER_TEST
        /* QEMU -kernel with an ELF payload leaves x0 empty; the test harness
         * stages the virt DTB at a fixed address with -device loader. */
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
    const uint32_t dtb_len = be32(fdt + 4);
    int result = cirrus_boot_plan_from_fdt_with_defaults(
        dtb, dtb_len, (uint64_t)(uintptr_t)dtb, BOARD_RAM_BASE,
        BOARD_RAM_SIZE, &plan);
    if (result != CIRRUS_BOOT_OK) {
        fail(cirrus_boot_error_string(result));
    }
    cirrus_console_puts(&console, "CIRRUS loader: board=");
    cirrus_console_puts(&console, plan.board_id);
    cirrus_console_puts(&console, " mem=");
    cirrus_console_put_u32(&console, (uint32_t)plan.usable_count);
    cirrus_console_puts(&console, " reserved=");
    cirrus_console_put_u32(&console, (uint32_t)plan.reserved_count);
    cirrus_console_puts(&console, "\n");

    /* Carve firmware-reserved ranges out of allocatable RAM first. */
    for (size_t i = 0; i < plan.reserved_count; i++) {
        reserve_or_fail("firmware carveout", plan.reserved[i].start,
                        plan.reserved[i].len);
    }
    const uint64_t loader_start = (uint64_t)(uintptr_t)__image_start;
    const uint64_t loader_len =
        (uint64_t)(uintptr_t)__image_end - loader_start;
    reserve_or_fail("loader image", loader_start, loader_len);
    reserve_or_fail("DTB", plan.dtb_start, plan.dtb_len);

    if (plan.initrd_len == 0u) {
        fail("no initrd in chosen");
    }
    if (!plan_covers(&plan, plan.initrd_start, plan.initrd_len)) {
        fail("initrd outside RAM");
    }
    console_hex("CIRRUS loader: initrd=", plan.initrd_start);

    const void *kernel_elf = NULL;
    size_t kernel_elf_len = 0;
    result = cirrus_initrd_find((const void *)(uintptr_t)plan.initrd_start,
                                plan.initrd_len, CIRRUS_INITRD_KERNEL_PATH,
                                &kernel_elf, &kernel_elf_len);
    if (result != CIRRUS_BOOT_OK) {
        fail("kernel ELF missing from initrd");
    }
    struct cirrus_elf_image image;
    result = cirrus_elf_parse(kernel_elf, kernel_elf_len, &image);
    if (result != CIRRUS_BOOT_OK) {
        fail("kernel ELF invalid");
    }
    const uint64_t span = image.virt_end - image.virt_base;
    if (span == 0u || (image.virt_base & (KERNEL_ALIGN - 1u)) != 0u) {
        fail("kernel virtual base unsupported");
    }
    uint64_t placement = 0;
    result = cirrus_boot_plan_find_placement(&plan, span, KERNEL_ALIGN,
                                             &placement);
    if (result != CIRRUS_BOOT_OK) {
        fail("no room for kernel image");
    }
    result = cirrus_elf_load(kernel_elf, kernel_elf_len, &image, placement,
                             loader_copy, loader_zero, NULL);
    if (result != CIRRUS_BOOT_OK) {
        fail("kernel load failed");
    }
    console_hex("CIRRUS loader: kernel phys=", placement);
    console_hex("CIRRUS loader: kernel virt=", image.virt_base);
    console_hex("CIRRUS loader: kernel entry=", image.entry);

    struct cirrus_bootinfo_inputs inputs;
    zero_bytes(&inputs, sizeof(inputs));
    inputs.kernel_phys_start = placement;
    inputs.kernel_phys_len = span;
    inputs.kernel_virt_start = image.virt_base;
    inputs.kernel_virt_len = span;
    inputs.loader_start = loader_start;
    inputs.loader_len = loader_len;
    size_t region_count = 0;
    result = cirrus_bootinfo_build(&plan, &inputs, regions,
                                   CIRRUS_BOOTINFO_MAX_REGIONS,
                                   &region_count, &bootinfo);
    if (result != CIRRUS_BOOT_OK) {
        fail("BootInfo build failed");
    }
    cirrus_console_puts(&console, "CIRRUS loader: bootinfo regions=");
    cirrus_console_put_u32(&console, (uint32_t)region_count);
    cirrus_console_puts(&console, "\n");

    const uint64_t ttbr1 =
        build_page_tables(&image, placement, loader_start, loader_len);
    cirrus_console_puts(&console, "CIRRUS loader: entering high half\n");
    enter_high(image.entry, ttbr1, &bootinfo);
}
