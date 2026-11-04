/* First bare-metal C smoke on KUMO: DebugWrite through the svc #0 trap,
 * a shared-memory IPC ring round trip, one HandleClose status call, then
 * ProcessExit. */
#include "kumo_ipc_ring.h"
#include "kumo_trap.h"

static const char smoke_banner[] =
    "CIRRUS C smoke: svc #0 trapped and returned\n";
static const char ring_banner[] =
    "CIRRUS C IPC ring: spsc ok\n";
static const char ring_failure[] =
    "CIRRUS C IPC ring: FAIL\n";

static _Alignas(64) uint8_t ring_region[KUMO_IPC_RING_REGION_SIZE(256)];

static int ring_probe(void)
{
    struct kumo_ipc_ring *ring =
        kumo_ipc_ring_init(ring_region, sizeof(ring_region), 256);
    if (ring == NULL) {
        return 0;
    }

    uint32_t sent = 0x5a17c0deu;
    uint32_t got = 0;
    uint16_t type = 0;
    uint32_t len = 0;

    if (kumo_ipc_ring_push(ring, 0x2a, &sent, sizeof(sent)) !=
        KUMO_IPC_RING_OK) {
        return 0;
    }
    if (kumo_ipc_ring_pop(ring, &type, &got, sizeof(got), &len) !=
        KUMO_IPC_RING_OK) {
        return 0;
    }
    if (type != 0x2a || len != sizeof(sent) || got != sent) {
        return 0;
    }
    return kumo_ipc_ring_empty(ring) == 1;
}

int main(void)
{
    uint64_t written = kumo_debug_write(smoke_banner, sizeof(smoke_banner) - 1);
    if (written != sizeof(smoke_banner) - 1) {
        kumo_process_exit(2);
    }

    if (!ring_probe()) {
        (void)kumo_debug_write(ring_failure, sizeof(ring_failure) - 1);
        kumo_process_exit(3);
    }
    if (kumo_debug_write(ring_banner, sizeof(ring_banner) - 1) !=
        sizeof(ring_banner) - 1) {
        kumo_process_exit(4);
    }

    /* Invalid handle: exercises the status-returning stub; BadHandle is expected. */
    (void)kumo_handle_close(KUMO_INVALID_HANDLE);

    kumo_process_exit(0);
}
