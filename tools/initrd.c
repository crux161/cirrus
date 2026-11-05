/* Deterministic KUMORD01 packer. The wire format belongs to KUMO ABI v3. */
#include "cirrus_initrd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* KUMO consumers copy the table into a fixed-size stack buffer. */
#define MAX_ENTRIES 48u

struct input {
    const char *path;
    unsigned char *bytes;
    size_t length;
};

static void put_le(unsigned char *p, uint64_t value, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        p[i] = (unsigned char)(value >> (i * 8));
    }
}

static int valid_path(const char *path)
{
    const size_t length = strlen(path);
    if (length == 0 || length >= CIRRUS_INITRD_PATH_MAX || path[0] == '/') {
        return 0;
    }
    const char *component = path;
    for (const char *p = path;; ++p) {
        if (*p == '/' || *p == '\0') {
            const size_t n = (size_t)(p - component);
            if (n == 0 || (n == 1 && component[0] == '.') ||
                (n == 2 && component[0] == '.' && component[1] == '.')) {
                return 0;
            }
            if (*p == '\0') {
                return 1;
            }
            component = p + 1;
        } else if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7e || *p == '\\') {
            return 0;
        }
    }
}

static int read_input(const char *source, struct input *input)
{
    FILE *file = fopen(source, "rb");
    if (file == NULL) {
        perror(source);
        return 0;
    }
    long length = -1;
    if (fseek(file, 0, SEEK_END) == 0) {
        length = ftell(file);
    }
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "initrd: cannot size %s\n", source);
        fclose(file);
        return 0;
    }
    input->length = (size_t)length;
    input->bytes = malloc(input->length == 0 ? 1 : input->length);
    if (input->bytes == NULL) {
        fclose(file);
        return 0;
    }
    const int ok = fread(input->bytes, 1, input->length, file) == input->length;
    const int closed = fclose(file) == 0;
    return ok && closed;
}

int main(int argc, char **argv)
{
    if (argc < 4 || (argc % 2) != 0 || (unsigned)(argc - 2) / 2 > MAX_ENTRIES) {
        fprintf(stderr, "usage: cirrus-initrd OUTPUT PATH SOURCE [PATH SOURCE ...] (max 48 entries)\n");
        return 2;
    }
    const size_t count = (size_t)(argc - 2) / 2;
    struct input inputs[MAX_ENTRIES] = {{0}};
    unsigned char table[CIRRUS_INITRD_HEADER_LEN + MAX_ENTRIES * CIRRUS_INITRD_ENTRY_LEN] = {0};
    const size_t table_length = CIRRUS_INITRD_HEADER_LEN + count * CIRRUS_INITRD_ENTRY_LEN;
    uint64_t offset = table_length;
    int status = 1;
    memcpy(table, CIRRUS_INITRD_MAGIC, 8);
    put_le(table + 8, CIRRUS_INITRD_VERSION, 4);
    put_le(table + 12, count, 4);
    for (size_t i = 0; i < count; ++i) {
        const char *path = argv[2 + i * 2];
        const char *source = argv[3 + i * 2];
        if (!valid_path(path) || strcmp(source, argv[1]) == 0) {
            fprintf(stderr, "initrd: invalid path or output used as input: %s\n", path);
            goto done;
        }
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(inputs[j].path, path) == 0) {
                fprintf(stderr, "initrd: duplicate path: %s\n", path);
                goto done;
            }
        }
        inputs[i].path = path;
        if (!read_input(source, &inputs[i]) || inputs[i].length > UINT64_MAX - offset) {
            fprintf(stderr, "initrd: cannot read input: %s\n", source);
            goto done;
        }
        unsigned char *entry = table + CIRRUS_INITRD_HEADER_LEN + i * CIRRUS_INITRD_ENTRY_LEN;
        memcpy(entry, path, strlen(path));
        put_le(entry + CIRRUS_INITRD_PATH_MAX, offset, 8);
        put_le(entry + CIRRUS_INITRD_PATH_MAX + 8, inputs[i].length, 8);
        offset += inputs[i].length;
    }
    FILE *output = fopen(argv[1], "wb");
    if (output == NULL) {
        perror(argv[1]);
        goto done;
    }
    int ok = fwrite(table, 1, table_length, output) == table_length;
    for (size_t i = 0; i < count && ok; ++i) {
        ok = fwrite(inputs[i].bytes, 1, inputs[i].length, output) == inputs[i].length;
    }
    if (fclose(output) != 0) {
        ok = 0;
    }
    if (!ok) {
        fprintf(stderr, "initrd: could not write %s\n", argv[1]);
        remove(argv[1]);
        goto done;
    }
    status = 0;
done:
    for (size_t i = 0; i < count; ++i) {
        free(inputs[i].bytes);
    }
    return status;
}
