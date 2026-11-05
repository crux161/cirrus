#include "cirrus_boot.h"

#define FDT_MAGIC 0xd00dfeedu
#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE 2u
#define FDT_PROP 3u
#define FDT_NOP 4u
#define FDT_END 9u
#define FDT_MAX_LEVEL 16u
#define FDT_NODE_NAME_MAX 64u

struct walker {
    const uint8_t *dtb;
    size_t len;
    uint32_t off_struct;
    uint32_t size_struct;
    uint32_t off_strings;
    uint32_t size_strings;
    uint32_t addr_cells;
    uint32_t size_cells;
    uint32_t rsv_addr_cells;
    uint32_t rsv_size_cells;
    int have_rsv_cells;
    uint32_t level;
    char names[FDT_MAX_LEVEL][FDT_NODE_NAME_MAX];
    uint64_t initrd_end;
    struct cirrus_boot_plan *plan;
};

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static uint64_t be64(const uint8_t *p)
{
    return ((uint64_t)be32(p) << 32) | (uint64_t)be32(p + 4);
}

static uint32_t align4(uint32_t value)
{
    return (value + 3u) & ~3u;
}

static size_t text_len(const char *text)
{
    size_t n = 0;
    while (text[n] != '\0') {
        n++;
    }
    return n;
}

static int text_equal(const char *a, const char *b)
{
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int name_equal(const char *name, uint32_t len, const char *literal)
{
    uint32_t i = 0;
    while (i < len && literal[i] != '\0') {
        if (name[i] != literal[i]) {
            return 0;
        }
        i++;
    }
    return i == len && literal[i] == '\0';
}

static int name_prefix(const char *name, const char *prefix)
{
    size_t i = 0;
    while (prefix[i] != '\0') {
        if (name[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return name[i] == '\0' || name[i] == '@';
}

static void copy_text(char *dst, size_t cap, const char *src, size_t len)
{
    size_t n = len < cap - 1u ? len : cap - 1u;
    for (size_t i = 0; i < n; i++) {
        dst[i] = src[i];
    }
    dst[n] = '\0';
}

static int string_list_has(const uint8_t *value, uint32_t len, const char *needle)
{
    uint32_t offset = 0;
    while (offset < len) {
        const char *entry = (const char *)(value + offset);
        uint32_t remaining = len - offset;
        uint32_t entry_len = 0;
        while (entry_len < remaining && entry[entry_len] != 0) {
            entry_len++;
        }
        if (entry_len < remaining && name_equal(entry, entry_len, needle)) {
            return 1;
        }
        offset += entry_len + 1u;
    }
    return 0;
}

static void first_string(char *dst, size_t cap, const uint8_t *value, uint32_t len)
{
    uint32_t n = 0;
    while (n < len && value[n] != 0) {
        n++;
    }
    copy_text(dst, cap, (const char *)value, n);
}

static int add_region(struct cirrus_boot_plan *plan, uint64_t start, uint64_t len,
                      uint32_t kind)
{
    if (len == 0) {
        if (kind == CIRRUS_BOOT_REGION_USABLE && start != 0) {
            plan->memory_base_hint = start;
        }
        return CIRRUS_BOOT_OK;
    }
    if (kind == CIRRUS_BOOT_REGION_USABLE) {
        if (plan->usable_count >= CIRRUS_BOOT_MAX_USABLE) {
            return CIRRUS_BOOT_E2BIG;
        }
        struct cirrus_boot_region *r = &plan->usable[plan->usable_count++];
        r->start = start;
        r->len = len;
        r->kind = kind;
        r->reserved = 0;
    } else {
        if (plan->reserved_count >= CIRRUS_BOOT_MAX_RESERVED) {
            return CIRRUS_BOOT_E2BIG;
        }
        struct cirrus_boot_region *r = &plan->reserved[plan->reserved_count++];
        r->start = start;
        r->len = len;
        r->kind = kind;
        r->reserved = 0;
    }
    return CIRRUS_BOOT_OK;
}

static uint64_t read_cells(const uint8_t *value, uint32_t cells)
{
    uint64_t result = 0;
    for (uint32_t i = 0; i < cells; i++) {
        result = (result << 32) | be32(value + i * 4u);
    }
    return result;
}

static int parse_reg(struct walker *w, const uint8_t *value, uint32_t len,
                     uint32_t ac, uint32_t sc, uint32_t kind)
{
    if (ac > 2u || sc > 2u || ac + sc == 0u) {
        return CIRRUS_BOOT_ENOTSUP;
    }
    uint32_t entry_bytes = (ac + sc) * 4u;
    if (len % entry_bytes != 0u) {
        return CIRRUS_BOOT_ECORRUPT;
    }
    for (uint32_t offset = 0; offset < len; offset += entry_bytes) {
        uint64_t start = read_cells(value + offset, ac);
        uint64_t size = read_cells(value + offset + ac * 4u, sc);
        int result = add_region(w->plan, start, size, kind);
        if (result != CIRRUS_BOOT_OK) {
            return result;
        }
    }
    return CIRRUS_BOOT_OK;
}

static int read_prop_u32(const uint8_t *value, uint32_t len, uint32_t *out)
{
    if (len != 4u) {
        return CIRRUS_BOOT_ECORRUPT;
    }
    *out = be32(value);
    return CIRRUS_BOOT_OK;
}

static int read_prop_cells(const uint8_t *value, uint32_t len, uint64_t *out)
{
    if (len == 4u) {
        *out = be32(value);
        return CIRRUS_BOOT_OK;
    }
    if (len == 8u) {
        *out = be64(value);
        return CIRRUS_BOOT_OK;
    }
    return CIRRUS_BOOT_ENOTSUP;
}

static void set_board_identity(struct cirrus_boot_plan *plan,
                               const uint8_t *compatible, uint32_t len)
{
    first_string(plan->compatible, sizeof(plan->compatible), compatible, len);
    if (string_list_has(compatible, len, "oneplus,enchilada")) {
        copy_text(plan->board_id, sizeof(plan->board_id),
                  "oneplus-enchilada", text_len("oneplus-enchilada"));
    } else if (string_list_has(compatible, len, "oneplus,fajita")) {
        copy_text(plan->board_id, sizeof(plan->board_id),
                  "oneplus-fajita", text_len("oneplus-fajita"));
    } else if (string_list_has(compatible, len, "qcom,sdm845")) {
        copy_text(plan->board_id, sizeof(plan->board_id), "sdm845", 6u);
    } else if (string_list_has(compatible, len, "linux,dummy-virt")) {
        copy_text(plan->board_id, sizeof(plan->board_id),
                  "qemu-virt-aarch64", text_len("qemu-virt-aarch64"));
    } else {
        copy_text(plan->board_id, sizeof(plan->board_id), "unknown", 7u);
    }
}

static int handle_root_prop(struct walker *w, const char *name, uint32_t name_len,
                            const uint8_t *value, uint32_t len)
{
    if (name_equal(name, name_len, "#address-cells")) {
        uint32_t cells = 0;
        int result = read_prop_u32(value, len, &cells);
        if (result != CIRRUS_BOOT_OK || cells > 2u) {
            return CIRRUS_BOOT_ENOTSUP;
        }
        w->addr_cells = cells;
    } else if (name_equal(name, name_len, "#size-cells")) {
        uint32_t cells = 0;
        int result = read_prop_u32(value, len, &cells);
        if (result != CIRRUS_BOOT_OK || cells > 2u) {
            return CIRRUS_BOOT_ENOTSUP;
        }
        w->size_cells = cells;
    } else if (name_equal(name, name_len, "compatible")) {
        set_board_identity(w->plan, value, len);
    }
    return CIRRUS_BOOT_OK;
}

static int handle_chosen_prop(struct walker *w, const char *name,
                              uint32_t name_len, const uint8_t *value,
                              uint32_t len)
{
    if (name_equal(name, name_len, "bootargs")) {
        uint32_t n = len;
        while (n > 0 && value[n - 1u] == 0) {
            n--;
        }
        copy_text(w->plan->cmdline, sizeof(w->plan->cmdline),
                  (const char *)value, n);
    } else if (name_equal(name, name_len, "linux,initrd-start")) {
        uint64_t start = 0;
        int result = read_prop_cells(value, len, &start);
        if (result != CIRRUS_BOOT_OK) {
            return result;
        }
        w->plan->initrd_start = start;
    } else if (name_equal(name, name_len, "linux,initrd-end")) {
        uint64_t end = 0;
        int result = read_prop_cells(value, len, &end);
        if (result != CIRRUS_BOOT_OK) {
            return result;
        }
        /* Property order is not guaranteed in a device tree; the length is
         * derived after the walk so an end-before-start tree parses too. */
        w->initrd_end = end;
    }
    return CIRRUS_BOOT_OK;
}

static int handle_prop(struct walker *w, const char *name, uint32_t name_len,
                       const uint8_t *value, uint32_t len)
{
    const char *node = w->names[w->level - 1u];
    if (w->level == 1u) {
        return handle_root_prop(w, name, name_len, value, len);
    }
    if (w->level == 2u) {
        if (name_prefix(node, "memory")) {
            if (name_equal(name, name_len, "reg")) {
                return parse_reg(w, value, len, w->addr_cells, w->size_cells,
                                 CIRRUS_BOOT_REGION_USABLE);
            }
        } else if (text_equal(node, "reserved-memory")) {
            if (!w->have_rsv_cells) {
                w->rsv_addr_cells = w->addr_cells;
                w->rsv_size_cells = w->size_cells;
                w->have_rsv_cells = 1;
            }
            if (name_equal(name, name_len, "#address-cells")) {
                uint32_t cells = 0;
                int result = read_prop_u32(value, len, &cells);
                if (result != CIRRUS_BOOT_OK || cells > 2u) {
                    return CIRRUS_BOOT_ENOTSUP;
                }
                w->rsv_addr_cells = cells;
            } else if (name_equal(name, name_len, "#size-cells")) {
                uint32_t cells = 0;
                int result = read_prop_u32(value, len, &cells);
                if (result != CIRRUS_BOOT_OK || cells > 2u) {
                    return CIRRUS_BOOT_ENOTSUP;
                }
                w->rsv_size_cells = cells;
            }
        } else if (text_equal(node, "chosen")) {
            return handle_chosen_prop(w, name, name_len, value, len);
        }
    } else if (w->level == 3u && text_equal(w->names[1], "reserved-memory")) {
        if (name_equal(name, name_len, "reg")) {
            return parse_reg(w, value, len, w->rsv_addr_cells,
                             w->rsv_size_cells, CIRRUS_BOOT_REGION_RESERVED);
        }
    }
    return CIRRUS_BOOT_OK;
}

static int bounded_name(const uint8_t *dtb, size_t len, uint32_t offset,
                        char *out, size_t cap)
{
    if (offset >= len) {
        return CIRRUS_BOOT_ECORRUPT;
    }
    size_t n = 0;
    while (offset + n < len && dtb[offset + n] != 0) {
        n++;
    }
    if (offset + n >= len || n >= cap) {
        return CIRRUS_BOOT_ECORRUPT;
    }
    copy_text(out, cap, (const char *)(dtb + offset), n);
    return CIRRUS_BOOT_OK;
}

static int walk_structure(struct walker *w)
{
    uint32_t pos = 0;
    uint32_t level = 0;
    while (pos + 4u <= w->size_struct) {
        uint32_t token = be32(w->dtb + w->off_struct + pos);
        pos += 4u;
        if (token == FDT_BEGIN_NODE) {
            if (level >= FDT_MAX_LEVEL) {
                return CIRRUS_BOOT_E2BIG;
            }
            int result = bounded_name(w->dtb, w->len,
                                      w->off_struct + pos,
                                      w->names[level], FDT_NODE_NAME_MAX);
            if (result != CIRRUS_BOOT_OK) {
                return result;
            }
            pos += align4((uint32_t)text_len(w->names[level]) + 1u);
            level++;
            w->level = level;
        } else if (token == FDT_END_NODE) {
            if (level == 0u) {
                return CIRRUS_BOOT_ECORRUPT;
            }
            level--;
            w->level = level;
        } else if (token == FDT_PROP) {
            if (pos + 8u > w->size_struct || level == 0u) {
                return CIRRUS_BOOT_ECORRUPT;
            }
            uint32_t len = be32(w->dtb + w->off_struct + pos);
            uint32_t nameoff = be32(w->dtb + w->off_struct + pos + 4u);
            pos += 8u;
            if (nameoff >= w->size_strings || pos + len > w->size_struct) {
                return CIRRUS_BOOT_ECORRUPT;
            }
            const char *name = (const char *)(w->dtb + w->off_strings + nameoff);
            uint32_t remaining = w->size_strings - nameoff;
            uint32_t name_len = 0;
            while (name_len < remaining && name[name_len] != 0) {
                name_len++;
            }
            if (name_len >= remaining) {
                return CIRRUS_BOOT_ECORRUPT;
            }
            const uint8_t *value = w->dtb + w->off_struct + pos;
            int result = handle_prop(w, name, name_len, value, len);
            if (result != CIRRUS_BOOT_OK) {
                return result;
            }
            pos += align4(len);
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            return level == 0u ? CIRRUS_BOOT_OK : CIRRUS_BOOT_ECORRUPT;
        } else {
            return CIRRUS_BOOT_ECORRUPT;
        }
    }
    return CIRRUS_BOOT_ECORRUPT;
}

static void zero_plan(struct cirrus_boot_plan *plan)
{
    uint8_t *p = (uint8_t *)plan;
    for (size_t i = 0; i < sizeof(*plan); i++) {
        p[i] = 0;
    }
}

int cirrus_boot_plan_from_fdt_with_defaults(const void *dtb, size_t dtb_len,
                                            uint64_t dtb_phys,
                                            uint64_t ram_base, uint64_t ram_size,
                                            struct cirrus_boot_plan *out)
{
    if (dtb == NULL || out == NULL || dtb_len < 40u) {
        return CIRRUS_BOOT_EINVAL;
    }
    const uint8_t *bytes = (const uint8_t *)dtb;
    if (be32(bytes) != FDT_MAGIC) {
        return CIRRUS_BOOT_EINVAL;
    }
    uint32_t total = be32(bytes + 4);
    if (total < 40u || total > dtb_len) {
        return CIRRUS_BOOT_ECORRUPT;
    }
    uint32_t off_struct = be32(bytes + 8);
    uint32_t off_strings = be32(bytes + 12);
    uint32_t off_rsvmap = be32(bytes + 16);
    uint32_t size_strings = be32(bytes + 32);
    uint32_t size_struct = be32(bytes + 36);
    if (off_struct + size_struct > total ||
        off_strings + size_strings > total) {
        return CIRRUS_BOOT_ECORRUPT;
    }

    zero_plan(out);
    out->dtb_start = dtb_phys;
    out->dtb_len = dtb_len;

    struct walker w;
    w.dtb = bytes;
    w.len = total;
    w.off_struct = off_struct;
    w.size_struct = size_struct;
    w.off_strings = off_strings;
    w.size_strings = size_strings;
    w.addr_cells = 2u;
    w.size_cells = 2u;
    w.rsv_addr_cells = 2u;
    w.rsv_size_cells = 2u;
    w.have_rsv_cells = 0;
    w.level = 0;
    w.initrd_end = 0;
    w.plan = out;
    for (uint32_t i = 0; i < FDT_MAX_LEVEL; i++) {
        w.names[i][0] = '\0';
    }

    uint32_t rsv = off_rsvmap;
    while (rsv + 16u <= total) {
        uint64_t start = be64(bytes + rsv);
        uint64_t len = be64(bytes + rsv + 8u);
        if (start == 0 && len == 0) {
            break;
        }
        int result = add_region(out, start, len, CIRRUS_BOOT_REGION_RESERVED);
        if (result != CIRRUS_BOOT_OK) {
            return result;
        }
        rsv += 16u;
    }

    int result = walk_structure(&w);
    if (result != CIRRUS_BOOT_OK) {
        return result;
    }
    if (w.initrd_end > out->initrd_start) {
        out->initrd_len = w.initrd_end - out->initrd_start;
    }
    if (out->usable_count == 0u && ram_size != 0u) {
        uint64_t base = ram_base != 0u ? ram_base : out->memory_base_hint;
        if (base != 0u) {
            int fallback = add_region(out, base, ram_size,
                                      CIRRUS_BOOT_REGION_USABLE);
            if (fallback != CIRRUS_BOOT_OK) {
                return fallback;
            }
        }
    }
    if (out->usable_count == 0u) {
        return CIRRUS_BOOT_ENODATA;
    }
    if (out->board_id[0] == '\0') {
        copy_text(out->board_id, sizeof(out->board_id), "unknown", 7u);
    }
    return CIRRUS_BOOT_OK;
}

int cirrus_boot_plan_from_fdt(const void *dtb, size_t dtb_len,
                              uint64_t dtb_phys,
                              struct cirrus_boot_plan *out)
{
    return cirrus_boot_plan_from_fdt_with_defaults(dtb, dtb_len, dtb_phys,
                                                   0u, 0u, out);
}

int cirrus_boot_plan_from_boot_image(const void *image, size_t image_len,
                                     uint64_t image_phys,
                                     struct cirrus_boot_plan *out)
{
    struct cirrus_boot_image parsed;
    int result = cirrus_boot_image_parse(image, image_len, &parsed);
    if (result != CIRRUS_BOOT_OK) {
        return result;
    }
    if (parsed.dtb_size == 0u || parsed.dtb == NULL) {
        return CIRRUS_BOOT_ENODATA;
    }
    result = cirrus_boot_plan_from_fdt(parsed.dtb, parsed.dtb_size,
                                       image_phys + parsed.dtb_offset, out);
    if (result != CIRRUS_BOOT_OK) {
        return result;
    }
    out->kernel_start = image_phys + parsed.kernel_offset;
    out->kernel_len = parsed.kernel_size;
    if (parsed.ramdisk_size != 0u) {
        out->initrd_start = image_phys + parsed.ramdisk_offset;
        out->initrd_len = parsed.ramdisk_size;
    }
    if (parsed.cmdline[0] != '\0') {
        copy_text(out->cmdline, sizeof(out->cmdline), parsed.cmdline,
                  text_len(parsed.cmdline));
    }
    return CIRRUS_BOOT_OK;
}

static uint64_t align_up64(uint64_t value, uint64_t align)
{
    return (value + align - 1u) & ~(align - 1u);
}

int cirrus_boot_plan_reserve(struct cirrus_boot_plan *plan,
                             uint64_t start, uint64_t len)
{
    if (plan == NULL) {
        return CIRRUS_BOOT_EINVAL;
    }
    if (len == 0u) {
        return CIRRUS_BOOT_OK;
    }
    if (start > ~0ull - len) {
        return CIRRUS_BOOT_ECORRUPT;
    }
    const uint64_t end = start + len;
    struct cirrus_boot_region out[CIRRUS_BOOT_MAX_USABLE];
    size_t count = 0;

    for (size_t i = 0; i < plan->usable_count; i++) {
        const struct cirrus_boot_region *region = &plan->usable[i];
        if (region->start > ~0ull - region->len) {
            return CIRRUS_BOOT_ECORRUPT;
        }
        const uint64_t region_end = region->start + region->len;
        if (region_end <= start || end <= region->start) {
            if (count >= CIRRUS_BOOT_MAX_USABLE) {
                return CIRRUS_BOOT_E2BIG;
            }
            out[count++] = *region;
            continue;
        }
        if (region->start < start) {
            if (count >= CIRRUS_BOOT_MAX_USABLE) {
                return CIRRUS_BOOT_E2BIG;
            }
            out[count].start = region->start;
            out[count].len = start - region->start;
            out[count].kind = region->kind;
            out[count].reserved = 0;
            count++;
        }
        if (end < region_end) {
            if (count >= CIRRUS_BOOT_MAX_USABLE) {
                return CIRRUS_BOOT_E2BIG;
            }
            out[count].start = end;
            out[count].len = region_end - end;
            out[count].kind = region->kind;
            out[count].reserved = 0;
            count++;
        }
    }

    for (size_t i = 0; i < count; i++) {
        plan->usable[i] = out[i];
    }
    plan->usable_count = count;
    return CIRRUS_BOOT_OK;
}

int cirrus_boot_plan_find_placement(const struct cirrus_boot_plan *plan,
                                    uint64_t span, uint64_t align,
                                    uint64_t *out)
{
    if (plan == NULL || out == NULL || span == 0u || align == 0u ||
        (align & (align - 1u)) != 0u) {
        return CIRRUS_BOOT_EINVAL;
    }
    const uint64_t initrd_start = plan->initrd_start;
    uint64_t initrd_end = initrd_start;
    if (plan->initrd_len != 0u) {
        if (initrd_start > ~0ull - plan->initrd_len) {
            return CIRRUS_BOOT_ECORRUPT;
        }
        initrd_end = initrd_start + plan->initrd_len;
    }

    for (size_t i = 0; i < plan->usable_count; i++) {
        const struct cirrus_boot_region *region = &plan->usable[i];
        if (region->start > ~0ull - region->len) {
            return CIRRUS_BOOT_ECORRUPT;
        }
        const uint64_t region_end = region->start + region->len;
        uint64_t candidate = align_up64(region->start, align);
        if (candidate < region->start) {
            return CIRRUS_BOOT_ECORRUPT;
        }
        while (candidate <= region_end && span <= region_end - candidate) {
            const uint64_t candidate_end = candidate + span;
            if (plan->initrd_len == 0u ||
                candidate_end <= initrd_start || initrd_end <= candidate) {
                *out = candidate;
                return CIRRUS_BOOT_OK;
            }
            uint64_t next = align_up64(initrd_end, align);
            if (next <= candidate) {
                return CIRRUS_BOOT_ECORRUPT;
            }
            candidate = next;
        }
    }
    return CIRRUS_BOOT_ENODATA;
}
