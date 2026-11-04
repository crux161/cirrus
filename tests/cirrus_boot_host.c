#include "cirrus_boot.h"
#include "cirrus_bootinfo.h"

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
        count != plan->usable_count + plan->reserved_count + 3u) {
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
    size_t image_len = cirrus_boot_image_pack_size(
        (uint32_t)page, (uint32_t)sizeof(kernel), (uint32_t)sizeof(ramdisk),
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
        image, image_len, (uint32_t)page, kernel, (uint32_t)sizeof(kernel),
        ramdisk, (uint32_t)sizeof(ramdisk), dtb, (uint32_t)dtb_len,
        "console=ttyMSM0,115200n8");
    if (result != CIRRUS_BOOT_OK) {
        fprintf(stderr, "pack failed: %s\n", cirrus_boot_error_string(result));
        free(image);
        return 1;
    }
    struct cirrus_boot_image parsed;
    result = cirrus_boot_image_parse(image, image_len, &parsed);
    if (result != CIRRUS_BOOT_OK || parsed.header_version != 2u ||
        parsed.kernel_size != sizeof(kernel) ||
        parsed.ramdisk_size != sizeof(ramdisk) ||
        parsed.dtb_size != dtb_len ||
        parsed.kernel_offset != page ||
        parsed.dtb_offset != align_up(
            align_up(page + sizeof(kernel), page) + sizeof(ramdisk), page)) {
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
    put_le32(image + 16, (uint32_t)ramdisk_len);
    put_le32(image + 36, (uint32_t)page);
    put_le32(image + 40, 2u);
    memcpy(image + 64, "console=ttyMSM0,115200n8", 24);
    put_le32(image + 1648, (uint32_t)dtb_len);
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
        parsed.ramdisk_size != ramdisk_len ||
        parsed.dtb_size != dtb_len ||
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
                 (synthetic &&
                  (test_pack_roundtrip(dtb, dtb_len) ||
                   test_boot_image(dtb, dtb_len)));
    free(dtb);
    if (!failed) {
        puts("cirrus-boot: host checks passed");
    }
    return failed ? 1 : 0;
}
