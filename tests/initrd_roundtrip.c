#include "cirrus_boot.h"
#include "cirrus_initrd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) return 1;
    unsigned char bytes[4096];
    const size_t length = fread(bytes, 1, sizeof(bytes), file);
    fclose(file);
    const void *payload = NULL;
    size_t payload_length = 0;
    if (length < 16 || bytes[12] != 2 ||
        cirrus_initrd_find(bytes, length, "boot/kumo-kernel.elf", &payload, &payload_length) != CIRRUS_BOOT_OK ||
        payload_length != 14 || memcmp(payload, "kernel fixture", 14) != 0) {
        return 1;
    }
    if (cirrus_initrd_find(bytes, length, "bin/sora", &payload, &payload_length) != CIRRUS_BOOT_OK ||
        payload_length != 0) {
        return 1;
    }
    if (cirrus_initrd_find(bytes, length, "absent", &payload, &payload_length) != CIRRUS_BOOT_ENODATA ||
        cirrus_initrd_find(bytes, length - 1, "boot/kumo-kernel.elf", &payload, &payload_length) != CIRRUS_BOOT_ECORRUPT) {
        return 1;
    }
    return 0;
}
