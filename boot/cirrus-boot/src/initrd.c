#include "cirrus_boot.h"
#include "cirrus_initrd.h"

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

static int magic_ok(const uint8_t *p)
{
    static const char magic[8] = {'K', 'U', 'M', 'O', 'R', 'D', '0', '1'};
    for (uint32_t i = 0; i < 8; i++) {
        if (p[i] != (uint8_t)magic[i]) {
            return 0;
        }
    }
    return 1;
}

static int path_equal(const uint8_t *field, const char *path)
{
    size_t i = 0;
    while (i < CIRRUS_INITRD_PATH_MAX && path[i] != '\0') {
        if (field[i] != (uint8_t)path[i]) {
            return 0;
        }
        i++;
    }
    if (i == CIRRUS_INITRD_PATH_MAX || path[i] != '\0') {
        return 0;
    }
    return field[i] == 0u;
}

int cirrus_initrd_find(const void *archive, size_t archive_len,
                       const char *path,
                       const void **out, size_t *out_len)
{
    if (archive == NULL || path == NULL || out == NULL || out_len == NULL) {
        return CIRRUS_BOOT_EINVAL;
    }
    if (archive_len < CIRRUS_INITRD_HEADER_LEN) {
        return CIRRUS_BOOT_ECORRUPT;
    }
    const uint8_t *bytes = (const uint8_t *)archive;
    if (!magic_ok(bytes)) {
        return CIRRUS_BOOT_ECORRUPT;
    }
    if (le32(bytes + 8) != CIRRUS_INITRD_VERSION) {
        return CIRRUS_BOOT_ENOTSUP;
    }
    const uint32_t count = le32(bytes + 12);
    if (count > 1024u) {
        return CIRRUS_BOOT_ECORRUPT;
    }
    const uint64_t table_end = (uint64_t)CIRRUS_INITRD_HEADER_LEN +
                               (uint64_t)count * CIRRUS_INITRD_ENTRY_LEN;
    if (table_end > archive_len) {
        return CIRRUS_BOOT_ECORRUPT;
    }
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *entry = bytes + CIRRUS_INITRD_HEADER_LEN +
                               (size_t)i * CIRRUS_INITRD_ENTRY_LEN;
        if (!path_equal(entry, path)) {
            continue;
        }
        const uint64_t offset = le64(entry + CIRRUS_INITRD_PATH_MAX);
        const uint64_t len = le64(entry + CIRRUS_INITRD_PATH_MAX + 8);
        if (offset < table_end || offset > archive_len) {
            return CIRRUS_BOOT_ECORRUPT;
        }
        if (len > (uint64_t)archive_len - offset) {
            return CIRRUS_BOOT_ECORRUPT;
        }
        *out = bytes + offset;
        *out_len = (size_t)len;
        return CIRRUS_BOOT_OK;
    }
    return CIRRUS_BOOT_ENODATA;
}
