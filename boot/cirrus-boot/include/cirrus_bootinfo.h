/*
 * CIRRUS C mirror of kumo_abi::BootInfo.
 *
 * The loader builds this structure and passes its physical address to KUMO
 * _start in x0. The layout is pinned against the Rust struct by static
 * assertions and by the host layout probe.
 */
#ifndef CIRRUS_BOOTINFO_H
#define CIRRUS_BOOTINFO_H

#include <stdint.h>
#include <stddef.h>

#include "cirrus_boot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CIRRUS_BOOTINFO_ABI_VERSION 3u
#define CIRRUS_BOOTINFO_BOARD_ID_MAX 32u
#define CIRRUS_BOOTINFO_MAX_REGIONS 128u

#define CIRRUS_BOOTINFO_FLAG_FRAMEBUFFER_PRESENT (1u << 0)
#define CIRRUS_BOOTINFO_FLAG_VERIFIED_BOOT_PRESENT (1u << 1)
#define CIRRUS_BOOTINFO_FLAG_FRAMEBUFFER_RESERVED_RAM (1u << 2)

struct cirrus_bootinfo_raw_slice {
    uint64_t ptr;
    uint64_t len;
};

struct cirrus_bootinfo_range {
    uint64_t start;
    uint64_t len;
};

struct cirrus_bootinfo_mem_region {
    struct cirrus_bootinfo_range range;
    uint32_t kind;
    uint32_t reserved;
};

struct cirrus_bootinfo_framebuffer {
    uint64_t phys;
    uint64_t len;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
};

struct cirrus_bootinfo_platform {
    uint64_t acpi_rsdp;
    uint64_t dtb;
    uint64_t pl011_console_base;
};

struct cirrus_bootinfo {
    uint32_t version;
    uint32_t flags;
    struct cirrus_bootinfo_raw_slice mem_regions;
    struct cirrus_bootinfo_range kernel_phys;
    struct cirrus_bootinfo_range kernel_virt;
    struct cirrus_bootinfo_range initrd;
    struct cirrus_bootinfo_framebuffer framebuffer;
    struct cirrus_bootinfo_platform platform;
    struct cirrus_bootinfo_raw_slice cmdline;
    uint8_t verified_boot_sig[64];
    uint8_t board_id[CIRRUS_BOOTINFO_BOARD_ID_MAX];
};

struct cirrus_bootinfo_inputs {
    uint64_t kernel_phys_start;
    uint64_t kernel_phys_len;
    uint64_t kernel_virt_start;
    uint64_t kernel_virt_len;
    /* Loader image span (text..bss), reported as a Bootloader region. */
    uint64_t loader_start;
    uint64_t loader_len;
    const char *cmdline; /* NULL uses plan->cmdline */
    const struct cirrus_bootinfo_framebuffer *framebuffer; /* optional */
    uint32_t extra_flags;
};

/*
 * Build a BootInfo from a boot plan. The caller owns `regions`; the returned
 * mem_regions slice points into it and must remain alive through handoff.
 */
int cirrus_bootinfo_build(const struct cirrus_boot_plan *plan,
                          const struct cirrus_bootinfo_inputs *in,
                          struct cirrus_bootinfo_mem_region *regions,
                          size_t region_capacity,
                          size_t *region_count,
                          struct cirrus_bootinfo *out);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#define CIRRUS_BOOTINFO_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define CIRRUS_BOOTINFO_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

CIRRUS_BOOTINFO_STATIC_ASSERT(sizeof(struct cirrus_bootinfo_raw_slice) == 16,
                              "raw slice layout");
CIRRUS_BOOTINFO_STATIC_ASSERT(sizeof(struct cirrus_bootinfo_range) == 16,
                              "range layout");
CIRRUS_BOOTINFO_STATIC_ASSERT(sizeof(struct cirrus_bootinfo_mem_region) == 24,
                              "mem region layout");
CIRRUS_BOOTINFO_STATIC_ASSERT(sizeof(struct cirrus_bootinfo_framebuffer) == 32,
                              "framebuffer layout");
CIRRUS_BOOTINFO_STATIC_ASSERT(sizeof(struct cirrus_bootinfo_platform) == 24,
                              "platform layout");
CIRRUS_BOOTINFO_STATIC_ASSERT(sizeof(struct cirrus_bootinfo) == 240,
                              "BootInfo size");
CIRRUS_BOOTINFO_STATIC_ASSERT(offsetof(struct cirrus_bootinfo, mem_regions) == 8,
                              "mem_regions offset");
CIRRUS_BOOTINFO_STATIC_ASSERT(offsetof(struct cirrus_bootinfo, kernel_phys) == 24,
                              "kernel_phys offset");
CIRRUS_BOOTINFO_STATIC_ASSERT(offsetof(struct cirrus_bootinfo, kernel_virt) == 40,
                              "kernel_virt offset");
CIRRUS_BOOTINFO_STATIC_ASSERT(offsetof(struct cirrus_bootinfo, initrd) == 56,
                              "initrd offset");
CIRRUS_BOOTINFO_STATIC_ASSERT(offsetof(struct cirrus_bootinfo, framebuffer) == 72,
                              "framebuffer offset");
CIRRUS_BOOTINFO_STATIC_ASSERT(offsetof(struct cirrus_bootinfo, platform) == 104,
                              "platform offset");
CIRRUS_BOOTINFO_STATIC_ASSERT(offsetof(struct cirrus_bootinfo, cmdline) == 128,
                              "cmdline offset");
CIRRUS_BOOTINFO_STATIC_ASSERT(offsetof(struct cirrus_bootinfo, verified_boot_sig) == 144,
                              "verified_boot_sig offset");
CIRRUS_BOOTINFO_STATIC_ASSERT(offsetof(struct cirrus_bootinfo, board_id) == 208,
                              "board_id offset");

#endif /* CIRRUS_BOOTINFO_H */
