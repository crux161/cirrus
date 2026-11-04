#include "kumo_ipc_ring.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static _Alignas(64) uint8_t region_a[KUMO_IPC_RING_REGION_SIZE(256)];
static _Alignas(64) uint8_t region_b[KUMO_IPC_RING_REGION_SIZE(128)];

static struct kumo_ipc_ring *fresh(uint8_t *region, size_t len,
                                   uint32_t capacity)
{
    struct kumo_ipc_ring *ring =
        kumo_ipc_ring_init(region, len, capacity);
    assert(ring != NULL);
    assert(kumo_ipc_ring_empty(ring));
    assert(kumo_ipc_ring_attach(region, len) == ring);
    return ring;
}

static void test_copy_and_zero(struct kumo_ipc_ring *ring)
{
    uint32_t value = 0x12345678u;
    uint32_t got = 0;
    uint16_t type = 0;
    uint32_t len = 0;

    assert(kumo_ipc_ring_push(ring, 7, &value, sizeof(value)) ==
           KUMO_IPC_RING_OK);
    assert(!kumo_ipc_ring_empty(ring));
    assert(kumo_ipc_ring_pop(ring, &type, &got, sizeof(got), &len) ==
           KUMO_IPC_RING_OK);
    assert(type == 7 && len == 4 && got == value);
    assert(kumo_ipc_ring_empty(ring));
}

static void test_reserve_release(struct kumo_ipc_ring *ring)
{
    void *slot = NULL;
    struct kumo_ipc_ring_token token;
    const void *payload = NULL;
    uint32_t len = 0;
    uint16_t type = 0;

    assert(kumo_ipc_ring_reserve(ring, 4, &slot, &token) ==
           KUMO_IPC_RING_OK);
    *(uint32_t *)slot = 0xdeadbeefu;
    assert(kumo_ipc_ring_commit(ring, &token, 9) == KUMO_IPC_RING_OK);
    assert(kumo_ipc_ring_acquire(ring, &type, &payload, &len, &token) ==
           KUMO_IPC_RING_OK);
    assert(type == 9 && len == 4);
    assert(*(const uint32_t *)payload == 0xdeadbeefu);
    assert(kumo_ipc_ring_release(ring, &token) == KUMO_IPC_RING_OK);
    assert(kumo_ipc_ring_empty(ring));
}

static void test_full_and_drain(struct kumo_ipc_ring *ring)
{
    uint8_t payload[100];
    memset(payload, 0xa5, sizeof(payload));
    uint8_t out[100];

    assert(kumo_ipc_ring_push(ring, 1, payload, sizeof(payload)) ==
           KUMO_IPC_RING_OK);
    assert(kumo_ipc_ring_push(ring, 2, payload, sizeof(payload)) ==
           KUMO_IPC_RING_OK);
    assert(kumo_ipc_ring_push(ring, 3, payload, sizeof(payload)) ==
           KUMO_IPC_RING_EAGAIN);

    uint16_t type = 0;
    uint32_t len = 0;
    assert(kumo_ipc_ring_pop(ring, &type, out, sizeof(out), &len) ==
           KUMO_IPC_RING_OK);
    assert(type == 1 && len == sizeof(payload));
    assert(memcmp(out, payload, sizeof(payload)) == 0);
    assert(kumo_ipc_ring_pop(ring, &type, out, sizeof(out), &len) ==
           KUMO_IPC_RING_OK);
    assert(type == 2 && len == sizeof(payload));
    assert(kumo_ipc_ring_empty(ring));
}

static void test_wrap_padding(struct kumo_ipc_ring *ring)
{
    uint8_t payload[40];
    uint8_t out[40];
    uint16_t type = 0;
    uint32_t len = 0;

    memset(payload, 0x5a, sizeof(payload));
    assert(kumo_ipc_ring_push(ring, 1, payload, sizeof(payload)) ==
           KUMO_IPC_RING_OK);
    assert(kumo_ipc_ring_pop(ring, &type, out, sizeof(out), &len) ==
           KUMO_IPC_RING_OK);
    assert(kumo_ipc_ring_push(ring, 2, payload, sizeof(payload)) ==
           KUMO_IPC_RING_OK);
    assert(kumo_ipc_ring_pop(ring, &type, out, sizeof(out), &len) ==
           KUMO_IPC_RING_OK);

    /* head is now 96 of 128; this record pads 32 bytes and wraps. */
    assert(kumo_ipc_ring_push(ring, 3, payload, sizeof(payload)) ==
           KUMO_IPC_RING_OK);
    assert(kumo_ipc_ring_pop(ring, &type, out, sizeof(out), &len) ==
           KUMO_IPC_RING_OK);
    assert(type == 3 && len == sizeof(payload));
    assert(memcmp(out, payload, sizeof(payload)) == 0);
    assert(kumo_ipc_ring_empty(ring));
}

int main(void)
{
    struct kumo_ipc_ring *ring = fresh(region_a, sizeof(region_a), 256);
    test_copy_and_zero(ring);
    test_reserve_release(ring);
    test_full_and_drain(ring);

    ring = fresh(region_b, sizeof(region_b), 128);
    test_wrap_padding(ring);

    puts("kumo_ipc_ring: host checks passed");
    return 0;
}
