#include "cirrus_boot.h"

#define BOOT_HEADER_V0_LEN 1632u
#define BOOT_HEADER_V1_LEN 1648u
#define BOOT_HEADER_V2_LEN 1660u
#define BOOT_NAME_LEN 16u
#define BOOT_CMDLINE_LEN 512u

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p)
{
    return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4) << 32);
}

static int magic_ok(const uint8_t *p)
{
    static const char magic[8] = {'A', 'N', 'D', 'R', 'O', 'I', 'D', '!'};
    for (uint32_t i = 0; i < 8; i++) {
        if (p[i] != (uint8_t)magic[i]) {
            return 0;
        }
    }
    return 1;
}

static int power_of_two(uint32_t value)
{
    return value != 0 && (value & (value - 1u)) == 0;
}

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1u) & ~(align - 1u);
}

static int range_ok(size_t image_len, uint64_t offset, uint64_t len)
{
    return offset <= (uint64_t)image_len &&
           len <= (uint64_t)image_len - offset;
}

static void copy_cmdline(char *destination, const uint8_t *source, size_t max)
{
    size_t i = 0;
    for (; i + 1 < max && source[i] != 0; i++) {
        destination[i] = (char)source[i];
    }
    destination[i] = '\0';
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void write_le64(uint8_t *p, uint64_t value)
{
    write_le32(p, (uint32_t)value);
    write_le32(p + 4, (uint32_t)(value >> 32));
}

static void copy_text_field(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    while (src != NULL && src[n] != '\0' && n + 1u < cap) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
}

static int layout_valid(const struct cirrus_boot_image_layout *layout)
{
    return layout != NULL &&
           power_of_two(layout->page_size) &&
           layout->page_size >= 512u &&
           layout->header_version <= 2u;
}

static int addr_ok(uint64_t value)
{
    return value <= 0xffffffffull;
}

void cirrus_boot_image_layout_default(uint32_t page_size,
                                      uint32_t header_version,
                                      struct cirrus_boot_image_layout *out)
{
    if (out == NULL) {
        return;
    }
    out->page_size = page_size;
    out->header_version = header_version;
    out->base = 0;
    out->kernel_offset = 0x00008000u;
    out->ramdisk_offset = 0x01000000u;
    out->second_offset = 0x00f00000u;
    out->tags_offset = 0x00000100u;
}

size_t cirrus_boot_image_pack_size(const struct cirrus_boot_image_layout *layout,
                                   uint32_t kernel_size,
                                   uint32_t ramdisk_size,
                                   uint32_t dtb_size)
{
    if (!layout_valid(layout)) {
        return SIZE_MAX;
    }
    if (layout->header_version < 2u && dtb_size != 0u) {
        return SIZE_MAX;
    }
    const uint64_t page = layout->page_size;
    uint64_t kernel_offset = page;
    uint64_t ramdisk_offset = align_up(kernel_offset + kernel_size, page);
    uint64_t dtb_offset = align_up(ramdisk_offset + ramdisk_size, page);
    uint64_t total = align_up(dtb_offset + dtb_size, page);
    if (total > (uint64_t)SIZE_MAX) {
        return SIZE_MAX;
    }
    return (size_t)total;
}

int cirrus_boot_image_pack(void *out, size_t out_len,
                           const struct cirrus_boot_image_layout *layout,
                           const void *kernel, uint32_t kernel_size,
                           const void *ramdisk, uint32_t ramdisk_size,
                           const void *dtb, uint32_t dtb_size,
                           const char *cmdline)
{
    if (out == NULL || !layout_valid(layout) ||
        (kernel_size != 0 && kernel == NULL) ||
        (ramdisk_size != 0 && ramdisk == NULL) ||
        (dtb_size != 0 && dtb == NULL)) {
        return CIRRUS_BOOT_EINVAL;
    }
    if (layout->header_version < 2u && dtb_size != 0u) {
        return CIRRUS_BOOT_ENOTSUP;
    }
    const uint64_t page = layout->page_size;
    const uint64_t kernel_addr = layout->base + layout->kernel_offset;
    const uint64_t ramdisk_addr = layout->base + layout->ramdisk_offset;
    const uint64_t second_addr = layout->base + layout->second_offset;
    const uint64_t tags_addr = layout->base + layout->tags_offset;
    if (!addr_ok(kernel_addr) || !addr_ok(ramdisk_addr) ||
        !addr_ok(second_addr) || !addr_ok(tags_addr)) {
        return CIRRUS_BOOT_EINVAL;
    }

    size_t required = cirrus_boot_image_pack_size(layout, kernel_size,
                                                   ramdisk_size, dtb_size);
    if (required == SIZE_MAX || out_len < required) {
        return CIRRUS_BOOT_EINVAL;
    }

    uint8_t *bytes = (uint8_t *)out;
    for (size_t i = 0; i < out_len; i++) {
        bytes[i] = 0;
    }
    const char magic[8] = {'A', 'N', 'D', 'R', 'O', 'I', 'D', '!'};
    for (uint32_t i = 0; i < 8; i++) {
        bytes[i] = (uint8_t)magic[i];
    }
    write_le32(bytes + 8, kernel_size);
    write_le32(bytes + 12, (uint32_t)kernel_addr);
    write_le32(bytes + 16, ramdisk_size);
    write_le32(bytes + 20, (uint32_t)ramdisk_addr);
    write_le32(bytes + 24, 0u);
    write_le32(bytes + 28, (uint32_t)second_addr);
    write_le32(bytes + 32, (uint32_t)tags_addr);
    write_le32(bytes + 36, layout->page_size);
    write_le32(bytes + 40, layout->header_version);
    write_le32(bytes + 44, 0u); /* os_version */
    copy_text_field((char *)bytes + 48, BOOT_NAME_LEN, "CIRRUS");
    copy_text_field((char *)bytes + 64, BOOT_CMDLINE_LEN, cmdline);

    const uint64_t kernel_offset = page;
    const uint64_t ramdisk_offset = align_up(kernel_offset + kernel_size, page);
    const uint64_t dtb_offset = align_up(ramdisk_offset + ramdisk_size, page);
    if (layout->header_version >= 1u) {
        write_le32(bytes + 1644,
                   layout->header_version >= 2u ? BOOT_HEADER_V2_LEN
                                                : BOOT_HEADER_V1_LEN);
    }
    if (layout->header_version >= 2u) {
        write_le32(bytes + 1648, dtb_size);
        write_le64(bytes + 1652, dtb_size != 0u
                                     ? layout->base + dtb_offset
                                     : 0u);
    }

    for (uint32_t i = 0; i < kernel_size; i++) {
        bytes[kernel_offset + i] = ((const uint8_t *)kernel)[i];
    }
    for (uint32_t i = 0; i < ramdisk_size; i++) {
        bytes[ramdisk_offset + i] = ((const uint8_t *)ramdisk)[i];
    }
    for (uint32_t i = 0; i < dtb_size; i++) {
        bytes[dtb_offset + i] = ((const uint8_t *)dtb)[i];
    }
    return CIRRUS_BOOT_OK;
}

int cirrus_boot_image_parse(const void *image, size_t image_len,
                            struct cirrus_boot_image *out)
{
    if (image == NULL || out == NULL || image_len < BOOT_HEADER_V0_LEN) {
        return CIRRUS_BOOT_EINVAL;
    }

    const uint8_t *bytes = (const uint8_t *)image;
    if (!magic_ok(bytes)) {
        return CIRRUS_BOOT_EINVAL;
    }

    uint32_t page_size = read_le32(bytes + 36);
    uint32_t header_version = read_le32(bytes + 40);
    if (!power_of_two(page_size) || page_size < 512u) {
        return CIRRUS_BOOT_EINVAL;
    }
    if (header_version > 2u) {
        return CIRRUS_BOOT_ENOTSUP;
    }

    uint32_t header_len = BOOT_HEADER_V0_LEN;
    if (header_version >= 1u) {
        header_len = BOOT_HEADER_V1_LEN;
    }
    if (header_version >= 2u) {
        header_len = BOOT_HEADER_V2_LEN;
    }
    if (image_len < header_len || image_len < page_size) {
        return CIRRUS_BOOT_ECORRUPT;
    }

    uint32_t kernel_size = read_le32(bytes + 8);
    uint32_t ramdisk_size = read_le32(bytes + 16);
    uint32_t second_size = read_le32(bytes + 24);
    uint32_t recovery_dtbo_size = 0;
    uint64_t recovery_dtbo_offset = 0;
    uint32_t dtb_size = 0;
    uint64_t dtb_addr = 0;

    if (header_version >= 1u) {
        recovery_dtbo_size = read_le32(bytes + 1632);
        recovery_dtbo_offset = read_le64(bytes + 1636);
    }
    if (header_version >= 2u) {
        dtb_size = read_le32(bytes + 1648);
        dtb_addr = read_le64(bytes + 1652);
    }

    uint64_t kernel_offset = page_size;
    uint64_t ramdisk_offset = align_up(kernel_offset + kernel_size, page_size);
    uint64_t second_offset = align_up(ramdisk_offset + ramdisk_size, page_size);
    uint64_t recovery_offset = 0;
    if (recovery_dtbo_size != 0) {
        recovery_offset = recovery_dtbo_offset != 0
                              ? recovery_dtbo_offset
                              : align_up(second_offset + second_size, page_size);
    }

    uint64_t dtb_offset = 0;
    if (dtb_size != 0) {
        uint64_t after = second_offset + second_size;
        if (recovery_dtbo_size != 0) {
            after = recovery_offset + recovery_dtbo_size;
        }
        dtb_offset = align_up(after, page_size);
    }

    if (!range_ok(image_len, kernel_offset, kernel_size) ||
        !range_ok(image_len, ramdisk_offset, ramdisk_size) ||
        !range_ok(image_len, second_offset, second_size) ||
        (recovery_dtbo_size != 0 &&
         !range_ok(image_len, recovery_offset, recovery_dtbo_size)) ||
        (dtb_size != 0 && !range_ok(image_len, dtb_offset, dtb_size))) {
        return CIRRUS_BOOT_ECORRUPT;
    }

    out->header_version = header_version;
    out->page_size = page_size;
    out->kernel_size = kernel_size;
    out->kernel_addr = read_le32(bytes + 12);
    out->ramdisk_size = ramdisk_size;
    out->ramdisk_addr = read_le32(bytes + 20);
    out->second_size = second_size;
    out->second_addr = read_le32(bytes + 28);
    out->tags_addr = read_le32(bytes + 32);
    out->recovery_dtbo_size = recovery_dtbo_size;
    out->dtb_size = dtb_size;
    out->recovery_dtbo_offset = recovery_offset;
    out->dtb_addr = dtb_addr;
    out->kernel_offset = kernel_offset;
    out->ramdisk_offset = ramdisk_offset;
    out->second_offset = second_offset;
    out->recovery_dtbo_file_offset = recovery_offset;
    out->dtb_offset = dtb_offset;
    out->kernel = kernel_size != 0 ? bytes + kernel_offset : NULL;
    out->ramdisk = ramdisk_size != 0 ? bytes + ramdisk_offset : NULL;
    out->second = second_size != 0 ? bytes + second_offset : NULL;
    out->dtb = dtb_size != 0 ? bytes + dtb_offset : NULL;
    copy_cmdline(out->cmdline, bytes + 64, sizeof(out->cmdline));
    return CIRRUS_BOOT_OK;
}

const char *cirrus_boot_error_string(int result)
{
    switch (result) {
    case CIRRUS_BOOT_OK:
        return "ok";
    case CIRRUS_BOOT_EINVAL:
        return "invalid argument";
    case CIRRUS_BOOT_ENODATA:
        return "missing data";
    case CIRRUS_BOOT_E2BIG:
        return "region table too small";
    case CIRRUS_BOOT_ECORRUPT:
        return "corrupt boot data";
    case CIRRUS_BOOT_ENOTSUP:
        return "unsupported format";
    default:
        return "unknown error";
    }
}
