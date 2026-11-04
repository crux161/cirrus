/*
 * CIRRUS KUMO IPC shared-memory ring.
 *
 * Single-producer/single-consumer byte ring over a caller-supplied, shared
 * region. The region is expected to be backed by a KUMO VMO and mapped into
 * both address spaces. The producer owns `head`; the consumer owns `tail`.
 * Records are length-prefixed and 8-byte aligned. When a record does not fit
 * before the end of the buffer, a PAD record advances the producer to offset
 * zero; the consumer skips it.
 *
 * The zero-copy API is reserve/commit on the producer side and
 * acquire/release on the consumer side. Push/pop are copy-based helpers.
 */
#ifndef CIRRUS_KUMO_IPC_RING_H
#define CIRRUS_KUMO_IPC_RING_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KUMO_IPC_RING_MAGIC 0x474e49524f4d554bULL /* "KUMORING" little-endian */
#define KUMO_IPC_RING_VERSION 1u
#define KUMO_IPC_RING_ALIGNMENT 64u
#define KUMO_IPC_RING_HEADER_SIZE 192u
#define KUMO_IPC_RING_RECORD_HEADER_SIZE 8u
#define KUMO_IPC_RING_FLAG_PAD 0x0001u

enum kumo_ipc_ring_result {
    KUMO_IPC_RING_OK = 0,
    KUMO_IPC_RING_EINVAL = -1,
    KUMO_IPC_RING_EAGAIN = -2,
    KUMO_IPC_RING_ECORRUPT = -3,
    KUMO_IPC_RING_ENOSPC = -4
};

/*
 * Shared header. `head` and `tail` must live on separate cache lines; the
 * reserved arrays make that layout explicit and the static assertions below
 * pin it. `data` starts at KUMO_IPC_RING_HEADER_SIZE.
 */
struct kumo_ipc_ring {
    uint64_t magic;
    uint32_t version;
    uint32_t capacity;
    uint32_t max_record;
    uint32_t flags;
    uint8_t reserved0[40];
    uint32_t head;
    uint8_t reserved1[60];
    uint32_t tail;
    uint8_t reserved2[60];
    uint8_t data[1];
};

struct kumo_ipc_ring_record {
    uint32_t len;
    uint16_t type;
    uint16_t flags;
};

struct kumo_ipc_ring_token {
    uint32_t expected_head;
    uint32_t expected_tail;
    uint32_t position;
    uint32_t total;
    uint32_t pad;
    uint32_t len;
};

#define KUMO_IPC_RING_REGION_SIZE(capacity) \
    ((size_t)KUMO_IPC_RING_HEADER_SIZE + (size_t)(capacity))

/* region must be 64-byte aligned, capacity a power of two >= 128. */
struct kumo_ipc_ring *kumo_ipc_ring_init(void *region, size_t region_len,
                                        uint32_t capacity);
struct kumo_ipc_ring *kumo_ipc_ring_attach(void *region, size_t region_len);

/* Producer: reserve a payload slot, write it, then commit it with a type. */
int kumo_ipc_ring_reserve(struct kumo_ipc_ring *ring, uint32_t len,
                          void **payload_out,
                          struct kumo_ipc_ring_token *token);
int kumo_ipc_ring_commit(struct kumo_ipc_ring *ring,
                         const struct kumo_ipc_ring_token *token, uint16_t type);

/* Consumer: acquire a zero-copy payload, then release it. */
int kumo_ipc_ring_acquire(struct kumo_ipc_ring *ring, uint16_t *type,
                          const void **payload_out, uint32_t *len_out,
                          struct kumo_ipc_ring_token *token);
int kumo_ipc_ring_release(struct kumo_ipc_ring *ring,
                          const struct kumo_ipc_ring_token *token);

/* Copy helpers. Pop leaves the record queued if out_cap is too small. */
int kumo_ipc_ring_push(struct kumo_ipc_ring *ring, uint16_t type,
                       const void *payload, uint32_t len);
int kumo_ipc_ring_pop(struct kumo_ipc_ring *ring, uint16_t *type, void *out,
                      uint32_t out_cap, uint32_t *len_out);

int kumo_ipc_ring_empty(const struct kumo_ipc_ring *ring);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#define KUMO_IPC_RING_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define KUMO_IPC_RING_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

KUMO_IPC_RING_STATIC_ASSERT(offsetof(struct kumo_ipc_ring, head) == 64,
                            "ring head cache-line offset");
KUMO_IPC_RING_STATIC_ASSERT(offsetof(struct kumo_ipc_ring, tail) == 128,
                            "ring tail cache-line offset");
KUMO_IPC_RING_STATIC_ASSERT(offsetof(struct kumo_ipc_ring, data) == 192,
                            "ring data offset");
KUMO_IPC_RING_STATIC_ASSERT(KUMO_IPC_RING_HEADER_SIZE ==
                                offsetof(struct kumo_ipc_ring, data),
                            "ring header size");
KUMO_IPC_RING_STATIC_ASSERT(sizeof(struct kumo_ipc_ring_record) == 8,
                            "ring record header size");

#endif /* CIRRUS_KUMO_IPC_RING_H */
