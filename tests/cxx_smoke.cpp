#include "kumo_trap.h"
#include "kumo_ipc_ring.h"

// Also link the raw trap, which has external C linkage for C++ callers.
KUMO_NORETURN static void fail()
{
    static constexpr char marker[] = "CIRRUS C++ smoke: FAIL\n";
    kumo_debug_write(marker, sizeof(marker) - 1);
    kumo_process_exit(1);
}

extern "C" int main(void)
{
    // Check the two-register return and nonzero argument/pointer marshalling.
    const auto pair = kumo_trap(KUMO_SYS_CHANNEL_CREATE, 0, 0, 0, 0, 0, 0);
    if (pair.x0 == KUMO_INVALID_U64 || pair.x1 == KUMO_INVALID_U64) {
        fail();
    }
    static constexpr char payload[] = "cxx";
    const auto sent = kumo_trap(KUMO_SYS_CHANNEL_WRITE, pair.x0,
        reinterpret_cast<uintptr_t>(payload), sizeof(payload), 0, 0, 0);
    char received[sizeof(payload)] = {};
    uint64_t handle = 0;
    if (kumo_status_from_x0(sent.x0) != KUMO_OK ||
        kumo_channel_read(static_cast<kumo_handle_t>(pair.x1), received,
            sizeof(received), &handle) != sizeof(payload) || handle != 0) {
        fail();
    }
    for (size_t i = 0; i < sizeof(payload); ++i) {
        if (received[i] != payload[i]) fail();
    }
    kumo_handle_close(static_cast<kumo_handle_t>(pair.x0));
    kumo_handle_close(static_cast<kumo_handle_t>(pair.x1));
    static constexpr char marker[] = "CIRRUS C++ smoke: raw trap returned\n";
    kumo_debug_write(marker, sizeof(marker) - 1);
    return 0;
}
