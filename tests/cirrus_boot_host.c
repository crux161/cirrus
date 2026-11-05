#include "cirrus_boot.h"
#include "cirrus_bootinfo.h"
#include "cirrus_console.h"
#include "cirrus_elf.h"
#include "cirrus_initrd.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *len_out)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *bytes = malloc((size_t)size);
    if (bytes == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *len_out = (size_t)size;
    return bytes;
}

static void put_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void put_le64(uint8_t *p, uint64_t value)
{
    put_le32(p, (uint32_t)value);
    put_le32(p + 4, (uint32_t)(value >> 32));
}

static size_t align_up(size_t value, size_t align)
{
    return (value + align - 1u) & ~(align - 1u);
}

static int test_bootinfo(const struct cirrus_boot_plan *plan, int synthetic)
{
    struct cirrus_bootinfo_mem_region regions[CIRRUS_BOOTINFO_MAX_REGIONS];
    struct cirrus_bootinfo_inputs inputs;
    memset(&inputs, 0, sizeof(inputs));
    inputs.kernel_phys_start = 0x50000000u;
    inputs.kernel_phys_len = 0x00200000u;
    inputs.kernel_virt_start = 0xffff800048000000ull;
    inputs.kernel_virt_len = 0x00200000u;
    inputs.loader_start = 0x40080000u;
    inputs.loader_len = 0x00020000u;

    struct cirrus_bootinfo info;
    size_t count = 0;
    int result = cirrus_bootinfo_build(plan, &inputs, regions,
                                       CIRRUS_BOOTINFO_MAX_REGIONS,
                                       &count, &info);
    if (result != CIRRUS_BOOT_OK) {
        fprintf(stderr, "BootInfo build failed: %s\n",
                cirrus_boot_error_string(result));
        return 1;
    }
    if (info.version != 3u || info.flags != 0u ||
        info.mem_regions.ptr != (uint64_t)(uintptr_t)regions ||
        info.mem_regions.len != count ||
        info.kernel_phys.start != inputs.kernel_phys_start ||
        info.kernel_phys.len != inputs.kernel_phys_len ||
        info.kernel_virt.start != inputs.kernel_virt_start ||
        info.kernel_virt.len != inputs.kernel_virt_len ||
        info.initrd.start != plan->initrd_start ||
        info.initrd.len != plan->initrd_len ||
        info.platform.dtb != plan->dtb_start ||
        strcmp((const char *)info.board_id, "oneplus-enchilada") != 0 ||
        info.cmdline.ptr != (uint64_t)(uintptr_t)plan->cmdline ||
        info.cmdline.len != strlen(plan->cmdline)) {
        fprintf(stderr, "BootInfo fields mismatch\n");
        return 1;
    }
    if (synthetic &&
        count != plan->usable_count + plan->reserved_count + 4u) {
        fprintf(stderr, "BootInfo region count mismatch: %zu\n", count);
        return 1;
    }

    struct cirrus_bootinfo_framebuffer framebuffer;
    memset(&framebuffer, 0, sizeof(framebuffer));
    framebuffer.phys = 0x90000000u;
    framebuffer.len = 0x00800000u;
    framebuffer.width = 1080u;
    framebuffer.height = 2280u;
    framebuffer.stride = 1080u;
    inputs.framebuffer = &framebuffer;
    inputs.extra_flags = 0;
    result = cirrus_bootinfo_build(plan, &inputs, regions,
                                   CIRRUS_BOOTINFO_MAX_REGIONS,
                                   &count, &info);
    if (result != CIRRUS_BOOT_OK ||
        (info.flags & CIRRUS_BOOTINFO_FLAG_FRAMEBUFFER_PRESENT) == 0u ||
        info.framebuffer.phys != framebuffer.phys ||
        info.framebuffer.width != framebuffer.width) {
        fprintf(stderr, "BootInfo framebuffer path mismatch\n");
        return 1;
    }

    result = cirrus_bootinfo_build(plan, &inputs, regions, 1u, &count, &info);
    if (result != CIRRUS_BOOT_E2BIG) {
        fprintf(stderr, "BootInfo capacity check failed\n");
        return 1;
    }
    return 0;
}

static int test_fdt(const uint8_t *dtb, size_t dtb_len, int synthetic)
{
    struct cirrus_boot_plan plan;
    int result;
    if (synthetic) {
        result = cirrus_boot_plan_from_fdt(dtb, dtb_len, 0x40000000u, &plan);
    } else {
        struct cirrus_boot_plan empty;
        int empty_result =
            cirrus_boot_plan_from_fdt(dtb, dtb_len, 0x40000000u, &empty);
        if (empty_result != CIRRUS_BOOT_ENODATA) {
            fprintf(stderr, "zero-size memory DTB should need board defaults, got %s\n",
                    cirrus_boot_error_string(empty_result));
            return 1;
        }
        result = cirrus_boot_plan_from_fdt_with_defaults(
            dtb, dtb_len, 0x40000000u, 0x80000000u, 0x40000000u, &plan);
    }
    if (result != CIRRUS_BOOT_OK) {
        fprintf(stderr, "FDT plan failed: %s\n", cirrus_boot_error_string(result));
        return 1;
    }

    printf("FDT plan: board=%s compatible=%s mem=%zu reserved=%zu cmdline='%s' initrd=%#" PRIx64 "+%#" PRIx64 "\n",
           plan.board_id, plan.compatible, plan.usable_count, plan.reserved_count,
           plan.cmdline, plan.initrd_start, plan.initrd_len);

    if (synthetic) {
        if (strcmp(plan.board_id, "oneplus-enchilada") != 0 ||
            strstr(plan.compatible, "oneplus,enchilada") == NULL) {
            fprintf(stderr, "synthetic board identity mismatch\n");
            return 1;
        }
        if (plan.usable_count != 1u ||
            plan.usable[0].start != 0x80000000ull ||
            plan.usable[0].len != 0x40000000ull) {
            fprintf(stderr, "synthetic memory region mismatch\n");
            return 1;
        }
        if (plan.reserved_count != 1u ||
            plan.reserved[0].start != 0x90000000ull ||
            plan.reserved[0].len != 0x100000ull) {
            fprintf(stderr, "synthetic reserved region mismatch\n");
            return 1;
        }
        if (strstr(plan.cmdline, "console=ttyMSM0") == NULL ||
            plan.initrd_start != 0x88000000ull ||
            plan.initrd_len != 0x100000ull) {
            fprintf(stderr, "synthetic chosen-node mismatch\n");
            return 1;
        }

        /* Reserve the carveout, then the DTB, and prove the usable range
         * splits around them and placement skips the initrd. */
        result = cirrus_boot_plan_reserve(&plan, 0x90000000ull, 0x100000ull);
        if (result != CIRRUS_BOOT_OK || plan.usable_count != 2u ||
            plan.usable[0].start != 0x80000000ull ||
            plan.usable[0].len != 0x10000000ull ||
            plan.usable[1].start != 0x90100000ull ||
            plan.usable[1].len != 0x2ff00000ull) {
            fprintf(stderr, "plan reserve split mismatch (%zu regions)\n",
                    plan.usable_count);
            return 1;
        }
        unsigned long long placement = 0;
        result = cirrus_boot_plan_find_placement(&plan, 0x00200000ull,
                                                 0x00200000ull, &placement);
        if (result != CIRRUS_BOOT_OK || placement != 0x80000000ull) {
            fprintf(stderr, "placement search mismatch: %s @ %llx\n",
                    cirrus_boot_error_string(result), placement);
            return 1;
        }
        result = cirrus_boot_plan_find_placement(&plan, 0x20000000ull,
                                                 0x00200000ull, &placement);
        if (result != CIRRUS_BOOT_OK || placement != 0x90200000ull) {
            fprintf(stderr, "placement initrd skip mismatch: %s @ %llx\n",
                    cirrus_boot_error_string(result), placement);
            return 1;
        }
    } else {
        if (strncmp(plan.board_id, "oneplus", 7) != 0 ||
            plan.usable_count != 1u ||
            plan.usable[0].start != 0x80000000ull ||
            plan.usable[0].len != 0x40000000ull ||
            plan.reserved_count == 0u) {
            fprintf(stderr, "real DTB plan mismatch\n");
            return 1;
        }
    }
    return test_bootinfo(&plan, synthetic);
}

static int test_pack_roundtrip(const uint8_t *dtb, size_t dtb_len)
{
    const size_t page = 2048u;
    uint8_t kernel[16];
    uint8_t ramdisk[32];
    for (size_t i = 0; i < sizeof(kernel); i++) {
        kernel[i] = (uint8_t)(0xa0u + i);
    }
    for (size_t i = 0; i < sizeof(ramdisk); i++) {
        ramdisk[i] = (uint8_t)(0x50u + i);
    }
    struct cirrus_boot_image_layout layout;
    cirrus_boot_image_layout_default((uint32_t)page, 2u, &layout);
    size_t image_len = cirrus_boot_image_pack_size(
        &layout, (uint32_t)sizeof(kernel), (uint32_t)sizeof(ramdisk),
        (uint32_t)dtb_len);
    if (image_len == SIZE_MAX) {
        fprintf(stderr, "pack size failed\n");
        return 1;
    }
    uint8_t *image = calloc(1, image_len);
    if (image == NULL) {
        return 1;
    }
    int result = cirrus_boot_image_pack(
        image, image_len, &layout, kernel, (uint32_t)sizeof(kernel),
        ramdisk, (uint32_t)sizeof(ramdisk), dtb, (uint32_t)dtb_len,
        "console=ttyMSM0,115200n8");
    if (result != CIRRUS_BOOT_OK) {
        fprintf(stderr, "pack failed: %s\n", cirrus_boot_error_string(result));
        free(image);
        return 1;
    }
    struct cirrus_boot_image parsed;
    result = cirrus_boot_image_parse(image, image_len, &parsed);
    const size_t dtb_offset = align_up(
        align_up(page + sizeof(kernel), page) + sizeof(ramdisk), page);
    if (result != CIRRUS_BOOT_OK || parsed.header_version != 2u ||
        parsed.kernel_size != sizeof(kernel) ||
        parsed.kernel_addr != 0x00008000u ||
        parsed.ramdisk_size != sizeof(ramdisk) ||
        parsed.ramdisk_addr != 0x01000000u ||
        parsed.tags_addr != 0x00000100u ||
        parsed.dtb_size != dtb_len ||
        parsed.dtb_addr != dtb_offset ||
        parsed.kernel_offset != page ||
        parsed.dtb_offset != dtb_offset) {
        fprintf(stderr, "pack roundtrip parse mismatch: %s\n",
                cirrus_boot_error_string(result));
        free(image);
        return 1;
    }
    struct cirrus_boot_plan plan;
    result = cirrus_boot_plan_from_boot_image(image, image_len, 0x40000000u,
                                              &plan);
    if (result != CIRRUS_BOOT_OK ||
        strcmp(plan.board_id, "oneplus-enchilada") != 0 ||
        plan.initrd_start != 0x40000000u + parsed.ramdisk_offset ||
        plan.dtb_start != 0x40000000u + parsed.dtb_offset) {
        fprintf(stderr, "pack roundtrip plan mismatch: %s\n",
                cirrus_boot_error_string(result));
        free(image);
        return 1;
    }

    /* OnePlus geometry: base + offsets land in SDM845 RAM. */
    cirrus_boot_image_layout_default(4096u, 2u, &layout);
    layout.base = 0x80000000ull;
    layout.kernel_offset = 0x00008000u;
    layout.ramdisk_offset = 0x02000000u;
    layout.tags_offset = 0x01e00000u;
    image_len = cirrus_boot_image_pack_size(&layout, 16u, 32u, 16u);
    uint8_t *op = calloc(1, image_len);
    result = cirrus_boot_image_pack(op, image_len, &layout, kernel, 16u,
                                    ramdisk, 32u, dtb, 16u, "");
    if (result != CIRRUS_BOOT_OK) {
        fprintf(stderr, "oneplus pack failed\n");
        free(image);
        free(op);
        return 1;
    }
    result = cirrus_boot_image_parse(op, image_len, &parsed);
    if (result != CIRRUS_BOOT_OK ||
        parsed.kernel_addr != 0x80008000u ||
        parsed.ramdisk_addr != 0x82000000u ||
        parsed.tags_addr != 0x81e00000u) {
        fprintf(stderr, "oneplus addresses mismatch\n");
        free(image);
        free(op);
        return 1;
    }
    free(op);
    free(image);
    return 0;
}

static int test_boot_image(const uint8_t *dtb, size_t dtb_len)
{
    const size_t page = 2048u;
    const size_t kernel_off = page;
    const size_t kernel_len = 16u;
    const size_t ramdisk_off = align_up(kernel_off + kernel_len, page);
    const size_t ramdisk_len = 32u;
    const size_t second_off = align_up(ramdisk_off + ramdisk_len, page);
    const size_t dtb_off = align_up(second_off, page);
    const size_t image_len = align_up(dtb_off + dtb_len, page);
    uint8_t *image = calloc(1, image_len);
    if (image == NULL) {
        return 1;
    }

    memcpy(image, "ANDROID!", 8);
    put_le32(image + 8, (uint32_t)kernel_len);
    put_le32(image + 12, 0x80008000u);
    put_le32(image + 16, (uint32_t)ramdisk_len);
    put_le32(image + 20, 0x82000000u);
    put_le32(image + 32, 0x81e00000u);
    put_le32(image + 36, (uint32_t)page);
    put_le32(image + 40, 2u);
    memcpy(image + 64, "console=ttyMSM0,115200n8", 24);
    put_le32(image + 1648, (uint32_t)dtb_len);
    put_le64(image + 1652, 0x80000000ull + dtb_off);
    for (size_t i = 0; i < kernel_len; i++) {
        image[kernel_off + i] = (uint8_t)(0xa0u + i);
    }
    for (size_t i = 0; i < ramdisk_len; i++) {
        image[ramdisk_off + i] = (uint8_t)(0x50u + i);
    }
    memcpy(image + dtb_off, dtb, dtb_len);

    struct cirrus_boot_image parsed;
    int result = cirrus_boot_image_parse(image, image_len, &parsed);
    if (result != CIRRUS_BOOT_OK) {
        fprintf(stderr, "boot image parse failed: %s\n",
                cirrus_boot_error_string(result));
        free(image);
        return 1;
    }
    if (parsed.header_version != 2u || parsed.page_size != page ||
        parsed.kernel_size != kernel_len ||
        parsed.kernel_addr != 0x80008000u ||
        parsed.ramdisk_size != ramdisk_len ||
        parsed.ramdisk_addr != 0x82000000u ||
        parsed.dtb_size != dtb_len ||
        parsed.dtb_addr != 0x80000000ull + dtb_off ||
        parsed.kernel_offset != kernel_off ||
        parsed.ramdisk_offset != ramdisk_off ||
        parsed.dtb_offset != dtb_off) {
        fprintf(stderr, "boot image layout mismatch\n");
        free(image);
        return 1;
    }

    struct cirrus_boot_plan plan;
    result = cirrus_boot_plan_from_boot_image(image, image_len,
                                              0x40000000u, &plan);
    if (result != CIRRUS_BOOT_OK ||
        strcmp(plan.board_id, "oneplus-enchilada") != 0 ||
        plan.kernel_start != 0x40000000u + kernel_off ||
        plan.initrd_start != 0x40000000u + ramdisk_off ||
        plan.dtb_start != 0x40000000u + dtb_off) {
        fprintf(stderr, "boot image plan mismatch: %s\n",
                cirrus_boot_error_string(result));
        free(image);
        return 1;
    }

    image[0] = 'X';
    result = cirrus_boot_image_parse(image, image_len, &parsed);
    if (result != CIRRUS_BOOT_EINVAL) {
        fprintf(stderr, "bad magic was not rejected\n");
        free(image);
        return 1;
    }
    free(image);
    return 0;
}

static int test_initrd(void)
{
    const char *kernel_path = CIRRUS_INITRD_KERNEL_PATH;
    const char *sora_path = "bin/sora";
    const size_t table = CIRRUS_INITRD_HEADER_LEN + 2u * CIRRUS_INITRD_ENTRY_LEN;
    const uint8_t kernel_payload[5] = {1, 2, 3, 4, 5};
    const uint8_t sora_payload[3] = {9, 8, 7};
    uint8_t archive[256];
    memset(archive, 0, sizeof(archive));
    memcpy(archive, CIRRUS_INITRD_MAGIC, 8);
    put_le32(archive + 8, 1u);
    put_le32(archive + 12, 2u);
    memcpy(archive + CIRRUS_INITRD_HEADER_LEN, kernel_path,
           strlen(kernel_path));
    put_le64(archive + CIRRUS_INITRD_HEADER_LEN + CIRRUS_INITRD_PATH_MAX, table);
    put_le64(archive + CIRRUS_INITRD_HEADER_LEN + CIRRUS_INITRD_PATH_MAX + 8,
             sizeof(kernel_payload));
    memcpy(archive + CIRRUS_INITRD_HEADER_LEN + CIRRUS_INITRD_ENTRY_LEN,
           sora_path, strlen(sora_path));
    put_le64(archive + CIRRUS_INITRD_HEADER_LEN + CIRRUS_INITRD_ENTRY_LEN +
                 CIRRUS_INITRD_PATH_MAX,
             table + sizeof(kernel_payload));
    put_le64(archive + CIRRUS_INITRD_HEADER_LEN + CIRRUS_INITRD_ENTRY_LEN +
                 CIRRUS_INITRD_PATH_MAX + 8,
             sizeof(sora_payload));
    memcpy(archive + table, kernel_payload, sizeof(kernel_payload));
    memcpy(archive + table + sizeof(kernel_payload), sora_payload,
           sizeof(sora_payload));

    const void *found = NULL;
    size_t found_len = 0;
    int result = cirrus_initrd_find(archive, sizeof(archive), kernel_path,
                                    &found, &found_len);
    if (result != CIRRUS_BOOT_OK || found != archive + table ||
        found_len != sizeof(kernel_payload)) {
        fprintf(stderr, "initrd find mismatch: %s\n",
                cirrus_boot_error_string(result));
        return 1;
    }
    result = cirrus_initrd_find(archive, sizeof(archive), "bin/missing",
                                &found, &found_len);
    if (result != CIRRUS_BOOT_ENODATA) {
        fprintf(stderr, "initrd missing-entry mismatch\n");
        return 1;
    }
    result = cirrus_initrd_find(archive, sizeof(archive),
                                "boot/kumo-kernel.elf-extra", &found,
                                &found_len);
    if (result != CIRRUS_BOOT_ENODATA) {
        fprintf(stderr, "initrd path-prefix mismatch\n");
        return 1;
    }
    archive[0] = 'X';
    result = cirrus_initrd_find(archive, sizeof(archive), kernel_path,
                                &found, &found_len);
    if (result != CIRRUS_BOOT_ECORRUPT) {
        fprintf(stderr, "initrd bad-magic mismatch\n");
        return 1;
    }

    /* GENI polled TX sequence against a mock MMIO array. */
    static uint32_t regs[0x900 / 4];
    memset(regs, 0, sizeof(regs));
    regs[0x610u / 4] = (1u << 30) | (1u << 0); /* watermark + done set */
    regs[0x40u / 4] = 0x00000001u;             /* SE alive */
    const uintptr_t base = (uintptr_t)regs;
    if (!cirrus_geni_probe(base)) {
        fprintf(stderr, "geni probe rejected mock\n");
        return 1;
    }
    cirrus_geni_init(base);
    cirrus_geni_putc(base, 'A');
    if (regs[0x600u / 4] != (1u << 27) ||
        regs[0x270u / 4] != 1u ||
        regs[0x700u / 4] != (uint32_t)'A' ||
        regs[0x80cu / 4] != 2u ||
        regs[0x260u / 4] != 0xfu) {
        fprintf(stderr, "geni register sequence mismatch\n");
        return 1;
    }
    return 0;
}

static void test_copy(void *ctx, uint64_t dest, const void *src, size_t len)
{
    uint8_t *base = (uint8_t *)ctx;
    memcpy(base + (dest - 0x10000u), src, len);
}

static void test_zero(void *ctx, uint64_t dest, size_t len)
{
    memset((uint8_t *)ctx + (dest - 0x10000u), 0, len);
}

static int test_elf(void)
{
    enum { EHDR = 64, PHDR = 56, DATA = 0x200 };
    uint8_t elf[DATA + 12];
    memset(elf, 0, sizeof(elf));
    memcpy(elf, "\x7f" "ELF", 4);
    elf[4] = 2;
    elf[5] = 1;
    elf[16] = 2; /* ET_EXEC */
    elf[18] = 0xb7; /* EM_AARCH64 */
    put_le64(elf + 24, 0xffff800000001000ull);
    put_le64(elf + 32, EHDR);
    elf[54] = PHDR;
    elf[56] = 2;
    /* segment 1: file 0x200, 8 bytes, paddr 0x1000, vaddr +delta, memsz 16 */
    put_le32(elf + EHDR, 1);
    put_le64(elf + EHDR + 8, DATA);
    put_le64(elf + EHDR + 16, 0xffff800000001000ull);
    put_le64(elf + EHDR + 24, 0x000000001000ull);
    put_le64(elf + EHDR + 32, 8u);
    put_le64(elf + EHDR + 40, 16u);
    /* segment 2: file 0x200+8, 4 bytes, paddr 0x2000, memsz 4 */
    put_le32(elf + EHDR + PHDR, 1);
    put_le64(elf + EHDR + PHDR + 8, DATA + 8);
    put_le64(elf + EHDR + PHDR + 16, 0xffff800000002000ull);
    put_le64(elf + EHDR + PHDR + 24, 0x000000002000ull);
    put_le64(elf + EHDR + PHDR + 32, 4u);
    put_le64(elf + EHDR + PHDR + 40, 4u);
    for (size_t i = 0; i < 8; i++) {
        elf[DATA + i] = (uint8_t)(0x10u + i);
    }
    for (size_t i = 0; i < 4; i++) {
        elf[DATA + 8 + i] = (uint8_t)(0x20u + i);
    }

    struct cirrus_elf_image image;
    int result = cirrus_elf_parse(elf, sizeof(elf), &image);
    if (result != CIRRUS_BOOT_OK || image.segment_count != 2u ||
        image.entry != 0xffff800000001000ull ||
        image.load_base != 0x1000u || image.load_end != 0x2004u ||
        image.virt_base != 0xffff800000001000ull ||
        image.virt_end != 0xffff800000002004ull ||
        image.virt_base - image.load_base != image.virt_phys_delta) {
        fprintf(stderr, "elf parse mismatch: %s\n",
                cirrus_boot_error_string(result));
        return 1;
    }

    uint8_t scratch[0x2000];
    memset(scratch, 0xee, sizeof(scratch));
    result = cirrus_elf_load(elf, sizeof(elf), &image, 0x10000u,
                             test_copy, test_zero, scratch);
    if (result != CIRRUS_BOOT_OK ||
        memcmp(scratch, elf + DATA, 8) != 0 ||
        scratch[8] != 0 || scratch[15] != 0 ||
        memcmp(scratch + 0x1000, elf + DATA + 8, 4) != 0) {
        fprintf(stderr, "elf load mismatch: %s\n",
                cirrus_boot_error_string(result));
        return 1;
    }
    elf[18] = 0x3e; /* x86-64 */
    result = cirrus_elf_parse(elf, sizeof(elf), &image);
    if (result != CIRRUS_BOOT_ENOTSUP) {
        fprintf(stderr, "elf machine check failed\n");
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <dtb> [synthetic|real]\n", argv[0]);
        return 2;
    }
    int synthetic = argc > 2 && strcmp(argv[2], "synthetic") == 0;
    size_t dtb_len = 0;
    uint8_t *dtb = read_file(argv[1], &dtb_len);
    if (dtb == NULL) {
        return 1;
    }
    int failed = test_fdt(dtb, dtb_len, synthetic) ||
                 test_initrd() || test_elf() ||
                 (synthetic &&
                  (test_pack_roundtrip(dtb, dtb_len) ||
                   test_boot_image(dtb, dtb_len)));
    free(dtb);
    if (!failed) {
        puts("cirrus-boot: host checks passed");
    }
    return failed ? 1 : 0;
}
