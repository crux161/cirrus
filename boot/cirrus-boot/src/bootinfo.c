#include "cirrus_bootinfo.h"

static void zero_bytes(void *destination, size_t len)
{
    uint8_t *dst = (uint8_t *)destination;
    for (size_t i = 0; i < len; i++) {
        dst[i] = 0;
    }
}

static size_t text_length(const char *text)
{
    size_t n = 0;
    while (text != NULL && text[n] != '\0') {
        n++;
    }
    return n;
}

static void copy_bytes(void *destination, const void *source, size_t len)
{
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    for (size_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

static int push_region(struct cirrus_bootinfo_mem_region *regions,
                       size_t capacity, size_t *count, uint64_t start,
                       uint64_t len, uint32_t kind)
{
    if (len == 0u) {
        return CIRRUS_BOOT_OK;
    }
    if (*count >= capacity || *count >= CIRRUS_BOOTINFO_MAX_REGIONS) {
        return CIRRUS_BOOT_E2BIG;
    }
    struct cirrus_bootinfo_mem_region *region = &regions[*count];
    region->range.start = start;
    region->range.len = len;
    region->kind = kind;
    region->reserved = 0;
    (*count)++;
    return CIRRUS_BOOT_OK;
}

int cirrus_bootinfo_build(const struct cirrus_boot_plan *plan,
                          const struct cirrus_bootinfo_inputs *in,
                          struct cirrus_bootinfo_mem_region *regions,
                          size_t region_capacity,
                          size_t *region_count,
                          struct cirrus_bootinfo *out)
{
    if (plan == NULL || in == NULL || regions == NULL ||
        region_count == NULL || out == NULL) {
        return CIRRUS_BOOT_EINVAL;
    }

    size_t count = 0;
    for (size_t i = 0; i < plan->usable_count; i++) {
        int result = push_region(regions, region_capacity, &count,
                                 plan->usable[i].start, plan->usable[i].len,
                                 plan->usable[i].kind);
        if (result != CIRRUS_BOOT_OK) {
            return result;
        }
    }
    for (size_t i = 0; i < plan->reserved_count; i++) {
        int result = push_region(regions, region_capacity, &count,
                                 plan->reserved[i].start, plan->reserved[i].len,
                                 plan->reserved[i].kind);
        if (result != CIRRUS_BOOT_OK) {
            return result;
        }
    }
    int result = push_region(regions, region_capacity, &count,
                             in->kernel_phys_start, in->kernel_phys_len,
                             CIRRUS_BOOT_REGION_KERNEL);
    if (result != CIRRUS_BOOT_OK) {
        return result;
    }
    result = push_region(regions, region_capacity, &count,
                         plan->initrd_start, plan->initrd_len,
                         CIRRUS_BOOT_REGION_INITRD);
    if (result != CIRRUS_BOOT_OK) {
        return result;
    }
    result = push_region(regions, region_capacity, &count,
                         plan->dtb_start, plan->dtb_len,
                         CIRRUS_BOOT_REGION_BOOTLOADER);
    if (result != CIRRUS_BOOT_OK) {
        return result;
    }
    if (count == 0u) {
        return CIRRUS_BOOT_ENODATA;
    }

    zero_bytes(out, sizeof(*out));
    out->version = CIRRUS_BOOTINFO_ABI_VERSION;
    out->flags = in->extra_flags;
    if (in->framebuffer != NULL) {
        out->flags |= CIRRUS_BOOTINFO_FLAG_FRAMEBUFFER_PRESENT;
        out->framebuffer = *in->framebuffer;
    }
    out->mem_regions.ptr = (uint64_t)(uintptr_t)regions;
    out->mem_regions.len = count;
    out->kernel_phys.start = in->kernel_phys_start;
    out->kernel_phys.len = in->kernel_phys_len;
    out->kernel_virt.start = in->kernel_virt_start;
    out->kernel_virt.len = in->kernel_virt_len;
    out->initrd.start = plan->initrd_start;
    out->initrd.len = plan->initrd_len;
    out->platform.dtb = plan->dtb_start;

    const char *cmdline = in->cmdline != NULL ? in->cmdline : plan->cmdline;
    out->cmdline.ptr = (uint64_t)(uintptr_t)cmdline;
    out->cmdline.len = text_length(cmdline);

    size_t board_len = text_length(plan->board_id);
    if (board_len >= CIRRUS_BOOTINFO_BOARD_ID_MAX) {
        board_len = CIRRUS_BOOTINFO_BOARD_ID_MAX - 1u;
    }
    copy_bytes(out->board_id, plan->board_id, board_len);
    out->board_id[board_len] = '\0';

    *region_count = count;
    return CIRRUS_BOOT_OK;
}
