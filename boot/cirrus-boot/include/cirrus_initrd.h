/*
 * CIRRUS KUMORD01 initrd archive lookup.
 *
 * The Android ramdisk carries a KUMORD01 archive with the KUMO kernel ELF
 * (boot/kumo-kernel.elf) and the first userland image (bin/sora). The loader
 * finds the kernel entry here before placing it in RAM.
 */
#ifndef CIRRUS_INITRD_H
#define CIRRUS_INITRD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CIRRUS_INITRD_MAGIC "KUMORD01"
#define CIRRUS_INITRD_HEADER_LEN 16u
#define CIRRUS_INITRD_ENTRY_LEN 80u
#define CIRRUS_INITRD_PATH_MAX 64u
#define CIRRUS_INITRD_VERSION 1u
#define CIRRUS_INITRD_KERNEL_PATH "boot/kumo-kernel.elf"

int cirrus_initrd_find(const void *archive, size_t archive_len,
                       const char *path,
                       const void **out, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* CIRRUS_INITRD_H */
