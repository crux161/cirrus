/*
 * Host tool: pack and inspect Android boot images with the CIRRUS C library so
 * the device-side parser and the build-side packer share one implementation.
 *
 *   bootimg info <image>
 *   bootimg pack <out.img> [options] <kernel> <ramdisk> [dtb]
 *
 * Options: --page-size N, --header-version 0|1|2, --base HEX,
 *          --kernel-offset HEX, --ramdisk-offset HEX,
 *          --second-offset HEX, --tags-offset HEX, --cmdline STR
 */
#include "cirrus_boot.h"

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
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *bytes = malloc((size_t)size + 1u);
    if (bytes == NULL) {
        fclose(file);
        return NULL;
    }
    if (size > 0 && fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *len_out = (size_t)size;
    return bytes;
}

static int write_file(const char *path, const void *bytes, size_t len)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        perror(path);
        return 0;
    }
    if (len > 0 && fwrite(bytes, 1, len, file) != len) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

static int parse_u64(const char *text, uint64_t *out)
{
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 0);
    if (end == text || *end != '\0') {
        return 0;
    }
    *out = (uint64_t)value;
    return 1;
}

static int info(const char *path)
{
    size_t len = 0;
    uint8_t *bytes = read_file(path, &len);
    if (bytes == NULL) {
        return 1;
    }
    struct cirrus_boot_image image;
    int result = cirrus_boot_image_parse(bytes, len, &image);
    if (result != CIRRUS_BOOT_OK) {
        fprintf(stderr, "bootimg: parse failed: %s\n",
                cirrus_boot_error_string(result));
        free(bytes);
        return 1;
    }
    printf("header_version %u\n", image.header_version);
    printf("page_size %u\n", image.page_size);
    printf("kernel_size %u kernel_addr 0x%08x\n", image.kernel_size,
           image.kernel_addr);
    printf("ramdisk_size %u ramdisk_addr 0x%08x\n", image.ramdisk_size,
           image.ramdisk_addr);
    printf("second_size %u second_addr 0x%08x\n", image.second_size,
           image.second_addr);
    printf("tags_addr 0x%08x\n", image.tags_addr);
    printf("dtb_size %u dtb_addr 0x%016" PRIx64 "\n", image.dtb_size,
           image.dtb_addr);
    printf("cmdline %s\n", image.cmdline);
    free(bytes);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s info <image>\n"
                        "       %s pack <out> [options] <kernel> <ramdisk> [dtb]\n",
                argv[0], argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "info") == 0) {
        return info(argv[2]);
    }
    if (strcmp(argv[1], "pack") != 0) {
        fprintf(stderr, "bootimg: unknown command '%s'\n", argv[1]);
        return 2;
    }

    struct cirrus_boot_image_layout layout;
    cirrus_boot_image_layout_default(4096u, 2u, &layout);
    const char *cmdline = "";
    const char *positional[3] = {NULL, NULL, NULL};
    int positional_count = 0;

    for (int i = 3; i < argc; i++) {
        const char *arg = argv[i];
        if (strncmp(arg, "--", 2) != 0) {
            if (positional_count >= 3) {
                fprintf(stderr, "bootimg: too many files\n");
                return 2;
            }
            positional[positional_count++] = arg;
            continue;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "bootimg: %s needs a value\n", arg);
            return 2;
        }
        const char *value = argv[++i];
        uint64_t number = 0;
        if (strcmp(arg, "--cmdline") == 0) {
            cmdline = value;
        } else if (strcmp(arg, "--page-size") == 0) {
            if (!parse_u64(value, &number)) {
                return 2;
            }
            layout.page_size = (uint32_t)number;
        } else if (strcmp(arg, "--header-version") == 0) {
            if (!parse_u64(value, &number)) {
                return 2;
            }
            layout.header_version = (uint32_t)number;
        } else if (strcmp(arg, "--base") == 0) {
            if (!parse_u64(value, &number)) {
                return 2;
            }
            layout.base = number;
        } else if (strcmp(arg, "--kernel-offset") == 0) {
            if (!parse_u64(value, &number)) {
                return 2;
            }
            layout.kernel_offset = (uint32_t)number;
        } else if (strcmp(arg, "--ramdisk-offset") == 0) {
            if (!parse_u64(value, &number)) {
                return 2;
            }
            layout.ramdisk_offset = (uint32_t)number;
        } else if (strcmp(arg, "--second-offset") == 0) {
            if (!parse_u64(value, &number)) {
                return 2;
            }
            layout.second_offset = (uint32_t)number;
        } else if (strcmp(arg, "--tags-offset") == 0) {
            if (!parse_u64(value, &number)) {
                return 2;
            }
            layout.tags_offset = (uint32_t)number;
        } else {
            fprintf(stderr, "bootimg: unknown option '%s'\n", arg);
            return 2;
        }
    }
    if (positional_count < 2) {
        fprintf(stderr, "bootimg: pack needs <kernel> <ramdisk> [dtb]\n");
        return 2;
    }

    size_t kernel_len = 0;
    size_t ramdisk_len = 0;
    size_t dtb_len = 0;
    uint8_t *kernel = read_file(positional[0], &kernel_len);
    uint8_t *ramdisk = read_file(positional[1], &ramdisk_len);
    uint8_t *dtb = positional[2] != NULL ? read_file(positional[2], &dtb_len)
                                         : NULL;
    if (kernel == NULL || ramdisk == NULL ||
        (positional[2] != NULL && dtb == NULL)) {
        free(kernel);
        free(ramdisk);
        free(dtb);
        return 1;
    }
    if (kernel_len > 0xffffffffu || ramdisk_len > 0xffffffffu ||
        dtb_len > 0xffffffffu) {
        fprintf(stderr, "bootimg: input file too large\n");
        return 1;
    }
    size_t image_len = cirrus_boot_image_pack_size(
        &layout, (uint32_t)kernel_len, (uint32_t)ramdisk_len,
        (uint32_t)dtb_len);
    if (image_len == SIZE_MAX) {
        fprintf(stderr, "bootimg: invalid layout or dtb with header < 2\n");
        return 1;
    }
    uint8_t *image = calloc(1, image_len);
    if (image == NULL) {
        return 1;
    }
    int result = cirrus_boot_image_pack(
        image, image_len, &layout, kernel, (uint32_t)kernel_len,
        ramdisk, (uint32_t)ramdisk_len,
        dtb, (uint32_t)dtb_len, cmdline);
    if (result != CIRRUS_BOOT_OK) {
        fprintf(stderr, "bootimg: pack failed: %s\n",
                cirrus_boot_error_string(result));
        return 1;
    }
    if (!write_file(argv[2], image, image_len)) {
        fprintf(stderr, "bootimg: write failed\n");
        return 1;
    }
    printf("bootimg: wrote %s (%zu bytes, header v%u)\n", argv[2], image_len,
           layout.header_version);
    free(image);
    free(kernel);
    free(ramdisk);
    free(dtb);
    return 0;
}
