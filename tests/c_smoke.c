/* First bare-metal C smoke on KUMO: DebugWrite through the svc #0 trap, a
 * shared-memory IPC ring round trip, a channel round trip, and a VMO-backed
 * cross-process IPC record, then ProcessExit. */
#include "kumo_ipc_ring.h"
#include "kumo_trap.h"

static const char smoke_banner[] =
    "CIRRUS C smoke: svc #0 trapped and returned\n";
static const char ring_banner[] =
    "CIRRUS C IPC ring: spsc ok\n";
static const char ring_failure[] =
    "CIRRUS C IPC ring: FAIL\n";
static const char channel_failure[] =
    "CIRRUS C channel: FAIL\n";
static const char child_banner[] =
    "CIRRUS C VMO IPC: child ran\n";
static const char vmo_banner[] =
    "CIRRUS C VMO IPC: cross-process ok\n";
static const char vmo_failure[] =
    "CIRRUS C VMO IPC: FAIL\n";

extern uint8_t __image_start[];
extern uint8_t __image_end[];

#define PAGE_SIZE 4096ull
#define RING_CAPACITY 256u
#define CHILD_VMAR_BASE 0x200000ull
#define CHILD_VMAR_SIZE 0x80000000ull
#define CHILD_IMAGE_BASE 0x200000ull
#define PARENT_RING_VA 0x10000000ull
#define CHILD_RING_VA 0x03000000ull
#define CHILD_STACK_TOP 0x40000000ull
#define CHILD_STACK_SIZE 0x00010000ull
#define CHILD_RECORD_TYPE 0x44u
#define CHILD_RECORD_MAGIC 0x5a17c0deu

static _Alignas(64) uint8_t ring_region[KUMO_IPC_RING_REGION_SIZE(RING_CAPACITY)];

#define VMO_FAIL(msg)                                                        \
    do {                                                                     \
        static const char text[] = "CIRRUS C VMO IPC: FAIL " msg "\n";      \
        (void)kumo_debug_write(text, sizeof(text) - 1);                      \
        return 0;                                                            \
    } while (0)

static uint64_t page_round(uint64_t value)
{
    return (value + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
}

static int bytes_equal(const void *a, const void *b, uint64_t len)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (uint64_t i = 0; i < len; i++) {
        if (x[i] != y[i]) {
            return 0;
        }
    }
    return 1;
}

static int ring_probe(void)
{
    struct kumo_ipc_ring *ring =
        kumo_ipc_ring_init(ring_region, sizeof(ring_region), RING_CAPACITY);
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

static int channel_probe(void)
{
    struct kumo_handles pair = kumo_channel_create();
    if (pair.first == KUMO_INVALID_U64 || pair.second == KUMO_INVALID_U64) {
        return 0;
    }
    const char payload[] = "vmo-ipc";
    char received[16];
    uint64_t transferred = 0;

    int ok = kumo_channel_write(pair.first, payload, sizeof(payload) - 1, 0) ==
                 KUMO_OK &&
             kumo_channel_read(pair.second, received, sizeof(received),
                               &transferred) == sizeof(payload) - 1 &&
             transferred == 0 &&
             bytes_equal(received, payload, sizeof(payload) - 1);
    (void)kumo_handle_close(pair.first);
    (void)kumo_handle_close(pair.second);
    return ok;
}

/* Runs in the child address space, entered with x0 = shared ring VA. The child
 * image is a read-only copy of the parent image mapped at its link address, and
 * the ring is the same VMO the parent initialized. */
__attribute__((used, noreturn)) void child_entry(uint64_t ring_va)
{
    (void)kumo_debug_write(child_banner, sizeof(child_banner) - 1);
    struct kumo_ipc_ring *ring = kumo_ipc_ring_attach(
        (void *)(uintptr_t)ring_va,
        KUMO_IPC_RING_REGION_SIZE(RING_CAPACITY));
    if (ring == NULL) {
        kumo_process_exit(5);
    }
    uint32_t magic = CHILD_RECORD_MAGIC;
    if (kumo_ipc_ring_push(ring, CHILD_RECORD_TYPE, &magic, sizeof(magic)) !=
        KUMO_IPC_RING_OK) {
        kumo_process_exit(6);
    }
    kumo_process_exit(0);
}

static int vmo_ipc_probe(void)
{
    const uint64_t ring_len = page_round(KUMO_IPC_RING_REGION_SIZE(RING_CAPACITY));
    const uint64_t image_span = (uint64_t)(__image_end - __image_start);
    const uint64_t image_len = page_round(image_span);

    uint64_t image_vmo = kumo_vmo_create(image_len);
    if (image_vmo == KUMO_INVALID_U64) {
        VMO_FAIL("image vmo");
    }
    if (kumo_vmo_write(image_vmo, 0, __image_start, image_span) != KUMO_OK) {
        VMO_FAIL("image write");
    }

    uint64_t ring_vmo = kumo_vmo_create(ring_len);
    if (ring_vmo == KUMO_INVALID_U64) {
        VMO_FAIL("ring vmo");
    }

    /* virt = 0 would ask the kernel to choose. The kernel's free-span search
     * does not yet know about the bootstrap image mappings, so an explicit VA
     * outside the image is required here. */
    struct kumo_status_value self_map = kumo_vmar_map(
        KUMO_INVALID_HANDLE, ring_vmo, 0, PARENT_RING_VA, ring_len,
        KUMO_VMAR_READ | KUMO_VMAR_WRITE | KUMO_VMAR_USER);
    if (self_map.status != KUMO_OK) {
        VMO_FAIL("ring map");
    }
    struct kumo_ipc_ring *ring = kumo_ipc_ring_init(
        (void *)(uintptr_t)self_map.value, ring_len, RING_CAPACITY);
    if (ring == NULL) {
        VMO_FAIL("ring init");
    }

    uint64_t child = kumo_process_create(CHILD_VMAR_BASE, CHILD_VMAR_SIZE);
    if (child == KUMO_INVALID_U64) {
        VMO_FAIL("process create");
    }
    if (kumo_vmar_map(child, image_vmo, 0, CHILD_IMAGE_BASE, image_len,
                      KUMO_VMAR_READ | KUMO_VMAR_EXECUTE | KUMO_VMAR_USER)
            .status != KUMO_OK) {
        VMO_FAIL("child image map");
    }
    if (kumo_vmar_map(child, ring_vmo, 0, CHILD_RING_VA, ring_len,
                      KUMO_VMAR_READ | KUMO_VMAR_WRITE | KUMO_VMAR_USER)
            .status != KUMO_OK) {
        VMO_FAIL("child ring map");
    }
    if (kumo_address_space_create(child, CHILD_STACK_TOP, CHILD_STACK_SIZE) ==
        KUMO_INVALID_U64) {
        VMO_FAIL("address space");
    }
    if (kumo_process_run(child, (uint64_t)(uintptr_t)&child_entry,
                         CHILD_STACK_TOP - 16u, CHILD_RING_VA, 0, 0) !=
        KUMO_OK) {
        VMO_FAIL("process run");
    }

    uint16_t type = 0;
    uint32_t magic = 0;
    uint32_t len = 0;
    if (kumo_ipc_ring_pop(ring, &type, &magic, sizeof(magic), &len) !=
        KUMO_IPC_RING_OK) {
        VMO_FAIL("record");
    }
    int ok = type == CHILD_RECORD_TYPE && len == sizeof(magic) &&
             magic == CHILD_RECORD_MAGIC;

    (void)kumo_handle_close(child);
    (void)kumo_handle_close(image_vmo);
    (void)kumo_handle_close(ring_vmo);
    return ok;
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

    if (!channel_probe()) {
        (void)kumo_debug_write(channel_failure, sizeof(channel_failure) - 1);
        kumo_process_exit(5);
    }

    if (!vmo_ipc_probe()) {
        (void)kumo_debug_write(vmo_failure, sizeof(vmo_failure) - 1);
        kumo_process_exit(6);
    }
    if (kumo_debug_write(vmo_banner, sizeof(vmo_banner) - 1) !=
        sizeof(vmo_banner) - 1) {
        kumo_process_exit(7);
    }

    /* Invalid handle: exercises the status-returning stub; BadHandle is expected. */
    (void)kumo_handle_close(KUMO_INVALID_HANDLE);

    kumo_process_exit(0);
}
