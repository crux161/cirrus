/* First bare-metal C smoke on KUMO: DebugWrite through the svc #0 trap,
 * one HandleClose status call, then ProcessExit. */
#include "kumo_trap.h"

static const char smoke_banner[] =
    "CIRRUS C smoke: svc #0 trapped and returned\n";

int main(void)
{
    uint64_t written = kumo_debug_write(smoke_banner, sizeof(smoke_banner) - 1);
    if (written != sizeof(smoke_banner) - 1) {
        kumo_process_exit(2);
    }

    /* Invalid handle: exercises the status-returning stub; BadHandle is expected. */
    (void)kumo_handle_close(KUMO_INVALID_HANDLE);

    kumo_process_exit(0);
}
