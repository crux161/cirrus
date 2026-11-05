/*
 * Minimal, bounds-checked ELF64 loader support for the KUMO kernel image.
 *
 * Only the ELF header and PT_LOAD segments are parsed: entry point, where each
 * segment's bytes live in the file, its linked physical/virtual addresses, and
 * how much memory it occupies. The loader places the image at a RAM address of
 * its choosing and maps the linked virtual range onto that placement.
 */
#ifndef CIRRUS_ELF_H
#define CIRRUS_ELF_H

#include <stdint.h>
#include <stddef.h>

#include "cirrus_boot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CIRRUS_ELF_MAX_SEGMENTS 16u
#define CIRRUS_ELF_EM_AARCH64 0xb7u

struct cirrus_elf_segment {
    uint64_t file_offset;
    uint64_t file_size;
    uint64_t phys_addr;
    uint64_t virt_addr;
    uint64_t mem_size;
};

struct cirrus_elf_image {
    uint64_t entry;
    uint64_t load_base;
    uint64_t load_end;
    uint64_t virt_base;
    uint64_t virt_end;
    /* virt_addr - phys_addr, uniform across every PT_LOAD segment. */
    uint64_t virt_phys_delta;
    size_t segment_count;
    struct cirrus_elf_segment segments[CIRRUS_ELF_MAX_SEGMENTS];
};

int cirrus_elf_parse(const void *elf, size_t elf_len,
                     struct cirrus_elf_image *out);

typedef void (*cirrus_elf_copy_fn)(void *ctx, uint64_t dest_phys,
                                   const void *src, size_t len);
typedef void (*cirrus_elf_zero_fn)(void *ctx, uint64_t dest_phys, size_t len);

/*
 * Copy each PT_LOAD segment to `placement + (p_paddr - load_base)` and zero
 * the mem_size-minus-file_size tail. The callbacks translate physical
 * addresses to whatever the current mapping allows.
 */
int cirrus_elf_load(const void *elf, size_t elf_len,
                    const struct cirrus_elf_image *image,
                    uint64_t placement,
                    cirrus_elf_copy_fn copy, cirrus_elf_zero_fn zero,
                    void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* CIRRUS_ELF_H */
