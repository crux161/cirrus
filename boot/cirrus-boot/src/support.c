/*
 * Freestanding runtime support for the CIRRUS loader.
 *
 * clang may lower struct assignment and zero-initialization to memcpy/memset,
 * and the loader links -nostdlib. These implementations are deliberately tiny;
 * the loader only copies boot data in a single-threaded pre-kernel context.
 */
#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t len)
{
    unsigned char *dst = (unsigned char *)destination;
    const unsigned char *src = (const unsigned char *)source;
    for (size_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
    return destination;
}

void *memset(void *destination, int value, size_t len)
{
    unsigned char *dst = (unsigned char *)destination;
    for (size_t i = 0; i < len; i++) {
        dst[i] = (unsigned char)value;
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t len)
{
    unsigned char *dst = (unsigned char *)destination;
    const unsigned char *src = (const unsigned char *)source;
    if (dst == src || len == 0u) {
        return destination;
    }
    if (dst < src) {
        for (size_t i = 0; i < len; i++) {
            dst[i] = src[i];
        }
    } else {
        for (size_t i = len; i != 0u; i--) {
            dst[i - 1u] = src[i - 1u];
        }
    }
    return destination;
}
