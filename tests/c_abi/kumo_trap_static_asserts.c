#include "kumo_trap.h"

KUMO_STATIC_ASSERT(KUMO_ABI_VERSION == 3u, "ABI version");
KUMO_STATIC_ASSERT(KUMO_SYSCALL_COUNT == 56u, "syscall count");
KUMO_STATIC_ASSERT(KUMO_SYS_VMO_CREATE_PAGED == 55, "last syscall");
KUMO_STATIC_ASSERT(sizeof(struct kumo_device_ctx_info) == 16, "ctx info");
KUMO_STATIC_ASSERT(sizeof(struct kumo_device_power_status) == 64, "power status");
KUMO_STATIC_ASSERT(sizeof(struct kumo_message_header) == 16, "message header");
KUMO_STATIC_ASSERT(sizeof(struct kumo_trap_result) == 16, "trap result");

int kumo_trap_header_probe(void) {
    struct kumo_trap_result result = {0, 0};
    return (int)(result.x0 + sizeof(struct kumo_device_power_status));
}

struct kumo_trap_result kumo_trap_probe_call(void) {
    return kumo_trap(KUMO_SYS_CLOCK_GET, 0, 0, 0, 0, 0, 0);
}
