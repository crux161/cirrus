#include "cirrus_elf.h"

#define ELF_EHDR_LEN 64u
#define ELF_PHDR_LEN 56u
#define ELF_ET_EXEC 2u
#define ELF_PT_LOAD 1u
#define ELF_MAX_PHDRS 256u

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

int cirrus_elf_parse(const void *elf, size_t elf_len,
                     struct cirrus_elf_image *out)
{
    if (elf == NULL || out == NULL) {
        return CIRRUS_BOOT_EINVAL;
    }
    if (elf_len < ELF_EHDR_LEN) {
        return CIRRUS_BOOT_ECORRUPT;
    }
    const uint8_t *bytes = (const uint8_t *)elf;
    if (bytes[0] != 0x7fu || bytes[1] != 'E' ||
        bytes[2] != 'L' || bytes[3] != 'F') {
        return CIRRUS_BOOT_EINVAL;
    }
    if (bytes[4] != 2u || bytes[5] != 1u) {
        return CIRRUS_BOOT_ENOTSUP;
    }
    if (le16(bytes + 16) != ELF_ET_EXEC ||
        le16(bytes + 18) != CIRRUS_ELF_EM_AARCH64) {
        return CIRRUS_BOOT_ENOTSUP;
    }
    const uint64_t entry = le64(bytes + 24);
    const uint64_t phoff = le64(bytes + 32);
    const uint16_t phentsize = le16(bytes + 54);
    const uint16_t phnum = le16(bytes + 56);
    if (phentsize != ELF_PHDR_LEN) {
        return CIRRUS_BOOT_ECORRUPT;
    }
    if (phnum == 0u || phnum > ELF_MAX_PHDRS) {
        return CIRRUS_BOOT_ECORRUPT;
    }
    if (phoff > elf_len ||
        (uint64_t)phnum * ELF_PHDR_LEN > (uint64_t)elf_len - phoff) {
        return CIRRUS_BOOT_ECORRUPT;
    }

    struct cirrus_elf_image image;
    for (size_t i = 0; i < sizeof(image); i++) {
        ((uint8_t *)&image)[i] = 0;
    }
    image.entry = entry;
    image.load_base = ~0ull;
    image.virt_base = ~0ull;
    int have_delta = 0;

    for (uint16_t i = 0; i < phnum; i++) {
        const uint8_t *ph = bytes + phoff + (size_t)i * ELF_PHDR_LEN;
        if (le32(ph) != ELF_PT_LOAD) {
            continue;
        }
        const uint64_t file_offset = le64(ph + 8);
        const uint64_t virt_addr = le64(ph + 16);
        const uint64_t phys_addr = le64(ph + 24);
        const uint64_t file_size = le64(ph + 32);
        const uint64_t mem_size = le64(ph + 40);
        if (file_size > mem_size) {
            return CIRRUS_BOOT_ECORRUPT;
        }
        if (file_offset > elf_len ||
            file_size > (uint64_t)elf_len - file_offset) {
            return CIRRUS_BOOT_ECORRUPT;
        }
        uint64_t load_end = phys_addr + mem_size;
        uint64_t virt_end = virt_addr + mem_size;
        if (load_end < phys_addr || virt_end < virt_addr) {
            return CIRRUS_BOOT_ECORRUPT;
        }
        if (!have_delta) {
            if (virt_addr < phys_addr) {
                return CIRRUS_BOOT_ENOTSUP;
            }
            image.virt_phys_delta = virt_addr - phys_addr;
            have_delta = 1;
        } else if (virt_addr - phys_addr != image.virt_phys_delta) {
            return CIRRUS_BOOT_ENOTSUP;
        }
        if (image.segment_count >= CIRRUS_ELF_MAX_SEGMENTS) {
            return CIRRUS_BOOT_E2BIG;
        }
        struct cirrus_elf_segment *seg = &image.segments[image.segment_count++];
        seg->file_offset = file_offset;
        seg->file_size = file_size;
        seg->phys_addr = phys_addr;
        seg->virt_addr = virt_addr;
        seg->mem_size = mem_size;
        if (phys_addr < image.load_base) {
            image.load_base = phys_addr;
        }
        if (load_end > image.load_end) {
            image.load_end = load_end;
        }
        if (virt_addr < image.virt_base) {
            image.virt_base = virt_addr;
        }
        if (virt_end > image.virt_end) {
            image.virt_end = virt_end;
        }
    }
    if (image.segment_count == 0u) {
        return CIRRUS_BOOT_ENODATA;
    }
    if (entry < image.virt_base || entry >= image.virt_end) {
        return CIRRUS_BOOT_ECORRUPT;
    }
    *out = image;
    return CIRRUS_BOOT_OK;
}

int cirrus_elf_load(const void *elf, size_t elf_len,
                    const struct cirrus_elf_image *image,
                    uint64_t placement,
                    cirrus_elf_copy_fn copy, cirrus_elf_zero_fn zero,
                    void *ctx)
{
    if (elf == NULL || image == NULL || copy == NULL || zero == NULL) {
        return CIRRUS_BOOT_EINVAL;
    }
    const uint8_t *bytes = (const uint8_t *)elf;
    for (size_t i = 0; i < image->segment_count; i++) {
        const struct cirrus_elf_segment *seg = &image->segments[i];
        uint64_t offset = seg->phys_addr - image->load_base;
        if (placement > ~0ull - offset) {
            return CIRRUS_BOOT_EINVAL;
        }
        const uint64_t dest = placement + offset;
        if (seg->file_size != 0u) {
            if (seg->file_offset > elf_len ||
                seg->file_size > (uint64_t)elf_len - seg->file_offset) {
                return CIRRUS_BOOT_ECORRUPT;
            }
            copy(ctx, dest, bytes + seg->file_offset, (size_t)seg->file_size);
        }
        const uint64_t bss = seg->mem_size - seg->file_size;
        if (bss != 0u) {
            zero(ctx, dest + seg->file_size, (size_t)bss);
        }
    }
    return CIRRUS_BOOT_OK;
}
