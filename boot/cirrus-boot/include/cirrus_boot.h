/*
 * CIRRUS freestanding boot-plan library.
 *
 * Parses an Android boot image and the board DTB into a hardware-independent
 * plan: memory regions, reserved regions, board identity, command line and the
 * initrd/kernel/DTB physical ranges. The loader turns this plan into
 * kumo_abi::BootInfo and then maps and enters KUMO.
 */
#ifndef CIRRUS_BOOT_H
#define CIRRUS_BOOT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CIRRUS_BOOT_MAX_USABLE 32u
#define CIRRUS_BOOT_MAX_RESERVED 64u
#define CIRRUS_BOOT_BOARD_ID_MAX 64u
#define CIRRUS_BOOT_COMPATIBLE_MAX 128u
#define CIRRUS_BOOT_CMDLINE_MAX 512u

enum cirrus_boot_result {
    CIRRUS_BOOT_OK = 0,
    CIRRUS_BOOT_EINVAL = -1,
    CIRRUS_BOOT_ENODATA = -2,
    CIRRUS_BOOT_E2BIG = -3,
    CIRRUS_BOOT_ECORRUPT = -4,
    CIRRUS_BOOT_ENOTSUP = -5
};

/* Matches kumo_abi::MemRegionKind values. */
enum cirrus_boot_region_kind {
    CIRRUS_BOOT_REGION_USABLE = 0,
    CIRRUS_BOOT_REGION_RESERVED = 1,
    CIRRUS_BOOT_REGION_ACPI = 2,
    CIRRUS_BOOT_REGION_MMIO = 3,
    CIRRUS_BOOT_REGION_BOOTLOADER = 4,
    CIRRUS_BOOT_REGION_KERNEL = 5,
    CIRRUS_BOOT_REGION_INITRD = 6,
    CIRRUS_BOOT_REGION_UNKNOWN = 0xffffffffu
};

struct cirrus_boot_region {
    uint64_t start;
    uint64_t len;
    uint32_t kind;
    uint32_t reserved;
};

struct cirrus_boot_plan {
    char board_id[CIRRUS_BOOT_BOARD_ID_MAX];
    char compatible[CIRRUS_BOOT_COMPATIBLE_MAX];
    char cmdline[CIRRUS_BOOT_CMDLINE_MAX];
    uint64_t dtb_start;
    uint64_t dtb_len;
    uint64_t memory_base_hint;
    uint64_t kernel_start;
    uint64_t kernel_len;
    uint64_t initrd_start;
    uint64_t initrd_len;
    struct cirrus_boot_region usable[CIRRUS_BOOT_MAX_USABLE];
    size_t usable_count;
    struct cirrus_boot_region reserved[CIRRUS_BOOT_MAX_RESERVED];
    size_t reserved_count;
};

struct cirrus_boot_image {
    uint32_t header_version;
    uint32_t page_size;
    uint32_t kernel_size;
    uint32_t ramdisk_size;
    uint32_t second_size;
    uint32_t recovery_dtbo_size;
    uint32_t dtb_size;
    uint64_t kernel_offset;
    uint64_t ramdisk_offset;
    uint64_t second_offset;
    uint64_t recovery_dtbo_offset;
    uint64_t dtb_offset;
    const uint8_t *kernel;
    const uint8_t *ramdisk;
    const uint8_t *second;
    const uint8_t *dtb;
    char cmdline[CIRRUS_BOOT_CMDLINE_MAX];
};

size_t cirrus_boot_image_pack_size(uint32_t page_size, uint32_t kernel_size,
                                       uint32_t ramdisk_size, uint32_t dtb_size);
int cirrus_boot_image_pack(void *out, size_t out_len, uint32_t page_size,
                           const void *kernel, uint32_t kernel_size,
                           const void *ramdisk, uint32_t ramdisk_size,
                           const void *dtb, uint32_t dtb_size,
                           const char *cmdline);
int cirrus_boot_image_parse(const void *image, size_t image_len,
                            struct cirrus_boot_image *out);
int cirrus_boot_plan_from_fdt(const void *dtb, size_t dtb_len,
                              uint64_t dtb_phys,
                              struct cirrus_boot_plan *out);
int cirrus_boot_plan_from_fdt_with_defaults(const void *dtb, size_t dtb_len,
                                            uint64_t dtb_phys,
                                            uint64_t ram_base, uint64_t ram_size,
                                            struct cirrus_boot_plan *out);
int cirrus_boot_plan_from_boot_image(const void *image, size_t image_len,
                                     uint64_t image_phys,
                                     struct cirrus_boot_plan *out);
const char *cirrus_boot_error_string(int result);

#ifdef __cplusplus
}
#endif

#endif /* CIRRUS_BOOT_H */
