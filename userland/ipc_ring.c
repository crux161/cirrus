#include "kumo_ipc_ring.h"

#define KUMO_LOAD_ACQUIRE(ptr) __atomic_load_n((ptr), __ATOMIC_ACQUIRE)
#define KUMO_LOAD_RELAXED(ptr) __atomic_load_n((ptr), __ATOMIC_RELAXED)
#define KUMO_STORE_RELEASE(ptr, value) \
    __atomic_store_n((ptr), (value), __ATOMIC_RELEASE)

static uint32_t align8(uint32_t value)
{
    return (value + 7u) & ~7u;
}

static int is_power_of_two(uint32_t value)
{
    return value != 0 && (value & (value - 1u)) == 0;
}

static void copy_bytes(void *destination, const void *source, uint32_t len)
{
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    for (uint32_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

static void zero_bytes(void *destination, uint32_t len)
{
    uint8_t *dst = (uint8_t *)destination;
    for (uint32_t i = 0; i < len; i++) {
        dst[i] = 0;
    }
}

static uint8_t *ring_data(struct kumo_ipc_ring *ring)
{
    return (uint8_t *)ring + KUMO_IPC_RING_HEADER_SIZE;
}

static uint32_t ring_mask(const struct kumo_ipc_ring *ring)
{
    return ring->capacity - 1u;
}

struct kumo_ipc_ring *kumo_ipc_ring_init(void *region, size_t region_len,
                                        uint32_t capacity)
{
    if (region == NULL ||
        ((uintptr_t)region & (KUMO_IPC_RING_ALIGNMENT - 1u)) != 0) {
        return NULL;
    }
    if (!is_power_of_two(capacity) || capacity < 128u ||
        capacity > (1u << 30)) {
        return NULL;
    }
    if (region_len < KUMO_IPC_RING_REGION_SIZE(capacity)) {
        return NULL;
    }

    struct kumo_ipc_ring *ring = (struct kumo_ipc_ring *)region;
    zero_bytes(ring, KUMO_IPC_RING_HEADER_SIZE);
    ring->magic = KUMO_IPC_RING_MAGIC;
    ring->version = KUMO_IPC_RING_VERSION;
    ring->capacity = capacity;
    ring->max_record = capacity - KUMO_IPC_RING_RECORD_HEADER_SIZE;
    ring->flags = 0;
    ring->head = 0;
    ring->tail = 0;
    return ring;
}

struct kumo_ipc_ring *kumo_ipc_ring_attach(void *region, size_t region_len)
{
    if (region == NULL ||
        ((uintptr_t)region & (KUMO_IPC_RING_ALIGNMENT - 1u)) != 0) {
        return NULL;
    }
    if (region_len < KUMO_IPC_RING_HEADER_SIZE) {
        return NULL;
    }

    struct kumo_ipc_ring *ring = (struct kumo_ipc_ring *)region;
    if (ring->magic != KUMO_IPC_RING_MAGIC ||
        ring->version != KUMO_IPC_RING_VERSION) {
        return NULL;
    }
    if (!is_power_of_two(ring->capacity) || ring->capacity < 128u ||
        ring->capacity > (1u << 30)) {
        return NULL;
    }
    if (ring->max_record !=
        ring->capacity - KUMO_IPC_RING_RECORD_HEADER_SIZE) {
        return NULL;
    }
    if (region_len < KUMO_IPC_RING_REGION_SIZE(ring->capacity)) {
        return NULL;
    }
    return ring;
}

int kumo_ipc_ring_reserve(struct kumo_ipc_ring *ring, uint32_t len,
                          void **payload_out,
                          struct kumo_ipc_ring_token *token)
{
    if (ring == NULL || payload_out == NULL || token == NULL) {
        return KUMO_IPC_RING_EINVAL;
    }
    if (len > ring->max_record) {
        return KUMO_IPC_RING_EINVAL;
    }

    uint32_t head = KUMO_LOAD_RELAXED(&ring->head);
    uint32_t tail = KUMO_LOAD_ACQUIRE(&ring->tail);
    uint32_t used = head - tail;
    if (used > ring->capacity) {
        return KUMO_IPC_RING_ECORRUPT;
    }
    uint32_t free_space = ring->capacity - used;
    uint32_t total = align8(KUMO_IPC_RING_RECORD_HEADER_SIZE + len);
    uint32_t position = head & ring_mask(ring);
    uint32_t remaining = ring->capacity - position;
    uint32_t pad = 0;
    uint32_t record_position = position;

    if (remaining < total) {
        pad = remaining;
        record_position = 0;
        if (pad < KUMO_IPC_RING_RECORD_HEADER_SIZE) {
            return KUMO_IPC_RING_EAGAIN;
        }
    }
    if (free_space < total + pad) {
        return KUMO_IPC_RING_EAGAIN;
    }

    token->expected_head = head;
    token->expected_tail = 0;
    token->position = record_position;
    token->total = total;
    token->pad = pad;
    token->len = len;
    *payload_out = ring_data(ring) +
                   record_position + KUMO_IPC_RING_RECORD_HEADER_SIZE;
    return KUMO_IPC_RING_OK;
}

int kumo_ipc_ring_commit(struct kumo_ipc_ring *ring,
                         const struct kumo_ipc_ring_token *token, uint16_t type)
{
    if (ring == NULL || token == NULL) {
        return KUMO_IPC_RING_EINVAL;
    }
    if (KUMO_LOAD_RELAXED(&ring->head) != token->expected_head) {
        return KUMO_IPC_RING_EINVAL;
    }

    uint8_t *data = ring_data(ring);
    if (token->pad != 0) {
        uint32_t pad_position = token->expected_head & ring_mask(ring);
        struct kumo_ipc_ring_record *pad_record =
            (struct kumo_ipc_ring_record *)(data + pad_position);
        pad_record->len = token->pad - KUMO_IPC_RING_RECORD_HEADER_SIZE;
        pad_record->type = 0;
        pad_record->flags = KUMO_IPC_RING_FLAG_PAD;
    }

    struct kumo_ipc_ring_record *record =
        (struct kumo_ipc_ring_record *)(data + token->position);
    record->len = token->len;
    record->type = type;
    record->flags = 0;

    KUMO_STORE_RELEASE(&ring->head,
                       token->expected_head + token->pad + token->total);
    return KUMO_IPC_RING_OK;
}

int kumo_ipc_ring_acquire(struct kumo_ipc_ring *ring, uint16_t *type,
                          const void **payload_out, uint32_t *len_out,
                          struct kumo_ipc_ring_token *token)
{
    if (ring == NULL || payload_out == NULL || len_out == NULL ||
        token == NULL) {
        return KUMO_IPC_RING_EINVAL;
    }

    for (;;) {
        uint32_t tail = KUMO_LOAD_RELAXED(&ring->tail);
        uint32_t head = KUMO_LOAD_ACQUIRE(&ring->head);
        uint32_t used = head - tail;
        if (used == 0) {
            return KUMO_IPC_RING_EAGAIN;
        }
        if (used > ring->capacity || used < KUMO_IPC_RING_RECORD_HEADER_SIZE) {
            return KUMO_IPC_RING_ECORRUPT;
        }

        uint32_t position = tail & ring_mask(ring);
        struct kumo_ipc_ring_record *record =
            (struct kumo_ipc_ring_record *)(ring_data(ring) + position);
        uint32_t record_len = record->len;
        uint16_t record_type = record->type;
        uint16_t record_flags = record->flags;
        uint32_t total =
            align8(KUMO_IPC_RING_RECORD_HEADER_SIZE + record_len);
        if (total < KUMO_IPC_RING_RECORD_HEADER_SIZE ||
            total > ring->capacity || total > used) {
            return KUMO_IPC_RING_ECORRUPT;
        }

        if ((record_flags & KUMO_IPC_RING_FLAG_PAD) != 0) {
            KUMO_STORE_RELEASE(&ring->tail, tail + total);
            continue;
        }
        if (record_len > ring->max_record) {
            return KUMO_IPC_RING_ECORRUPT;
        }

        token->expected_head = head;
        token->expected_tail = tail;
        token->position = position;
        token->total = total;
        token->pad = 0;
        token->len = record_len;
        if (type != NULL) {
            *type = record_type;
        }
        *payload_out = ring_data(ring) + position +
                       KUMO_IPC_RING_RECORD_HEADER_SIZE;
        *len_out = record_len;
        return KUMO_IPC_RING_OK;
    }
}

int kumo_ipc_ring_release(struct kumo_ipc_ring *ring,
                          const struct kumo_ipc_ring_token *token)
{
    if (ring == NULL || token == NULL) {
        return KUMO_IPC_RING_EINVAL;
    }
    if (KUMO_LOAD_RELAXED(&ring->tail) != token->expected_tail) {
        return KUMO_IPC_RING_EINVAL;
    }
    KUMO_STORE_RELEASE(&ring->tail, token->expected_tail + token->total);
    return KUMO_IPC_RING_OK;
}

int kumo_ipc_ring_push(struct kumo_ipc_ring *ring, uint16_t type,
                       const void *payload, uint32_t len)
{
    if (len != 0 && payload == NULL) {
        return KUMO_IPC_RING_EINVAL;
    }
    void *slot = NULL;
    struct kumo_ipc_ring_token token;
    int result = kumo_ipc_ring_reserve(ring, len, &slot, &token);
    if (result != KUMO_IPC_RING_OK) {
        return result;
    }
    if (len != 0) {
        copy_bytes(slot, payload, len);
    }
    return kumo_ipc_ring_commit(ring, &token, type);
}

int kumo_ipc_ring_pop(struct kumo_ipc_ring *ring, uint16_t *type, void *out,
                      uint32_t out_cap, uint32_t *len_out)
{
    if (out_cap != 0 && out == NULL) {
        return KUMO_IPC_RING_EINVAL;
    }

    const void *payload = NULL;
    uint32_t len = 0;
    uint16_t record_type = 0;
    struct kumo_ipc_ring_token token;
    int result = kumo_ipc_ring_acquire(ring, &record_type, &payload, &len,
                                       &token);
    if (result != KUMO_IPC_RING_OK) {
        return result;
    }
    if (out_cap < len) {
        return KUMO_IPC_RING_ENOSPC;
    }
    if (len != 0) {
        copy_bytes(out, payload, len);
    }
    result = kumo_ipc_ring_release(ring, &token);
    if (result != KUMO_IPC_RING_OK) {
        return result;
    }
    if (type != NULL) {
        *type = record_type;
    }
    if (len_out != NULL) {
        *len_out = len;
    }
    return KUMO_IPC_RING_OK;
}

int kumo_ipc_ring_empty(const struct kumo_ipc_ring *ring)
{
    if (ring == NULL) {
        return 1;
    }
    uint32_t head = KUMO_LOAD_ACQUIRE((uint32_t *)&ring->head);
    uint32_t tail = KUMO_LOAD_RELAXED((uint32_t *)&ring->tail);
    return head == tail;
}
