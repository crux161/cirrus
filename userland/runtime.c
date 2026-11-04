/* Minimal C runtime stubs over the KUMO/MUREX trap ABI. */
#include "kumo_trap.h"

kumo_status_t kumo_handle_close(kumo_handle_t handle)
{
    return kumo_status_from_x0(kumo_trap(KUMO_SYS_HANDLE_CLOSE, handle, 0, 0, 0, 0, 0).x0);
}

uint64_t kumo_debug_write(const void *bytes, uint64_t length)
{
    return kumo_trap(KUMO_SYS_DEBUG_WRITE, (uint64_t)(uintptr_t)bytes, length, 0, 0, 0, 0).x0;
}

KUMO_NORETURN void kumo_process_exit(uint64_t code)
{
    (void)kumo_trap(KUMO_SYS_PROCESS_EXIT, code, 0, 0, 0, 0, 0);
    for (;;) {
    }
}
