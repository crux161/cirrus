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

uint64_t kumo_handle_duplicate(kumo_handle_t handle, uint32_t rights)
{
    return kumo_trap(KUMO_SYS_HANDLE_DUPLICATE, handle, rights, 0, 0, 0, 0).x0;
}

uint64_t kumo_handle_koid(kumo_handle_t handle)
{
    return kumo_trap(KUMO_SYS_HANDLE_KOID, handle, 0, 0, 0, 0, 0).x0;
}

struct kumo_handles kumo_channel_create(void)
{
    struct kumo_trap_result result =
        kumo_trap(KUMO_SYS_CHANNEL_CREATE, 0, 0, 0, 0, 0, 0);
    struct kumo_handles handles;
    handles.first = result.x0;
    handles.second = result.x1;
    return handles;
}

kumo_status_t kumo_channel_write(kumo_handle_t channel, const void *bytes,
                                 uint64_t length, kumo_handle_t transfer_handle)
{
    return kumo_status_from_x0(
        kumo_trap(KUMO_SYS_CHANNEL_WRITE, channel,
                  (uint64_t)(uintptr_t)bytes, length, transfer_handle, 0, 0)
            .x0);
}

uint64_t kumo_channel_read(kumo_handle_t channel, void *buffer,
                           uint64_t capacity, uint64_t *handle_out)
{
    struct kumo_trap_result result =
        kumo_trap(KUMO_SYS_CHANNEL_READ, channel,
                  (uint64_t)(uintptr_t)buffer, capacity, 0, 0, 0);
    if (handle_out != NULL) {
        *handle_out = result.x1;
    }
    return result.x0;
}

uint64_t kumo_vmo_create(uint64_t size)
{
    return kumo_trap(KUMO_SYS_VMO_CREATE, size, 0, 0, 0, 0, 0).x0;
}

kumo_status_t kumo_vmo_read(kumo_handle_t vmo, uint64_t offset,
                            void *destination, uint64_t length)
{
    uint8_t *dst = (uint8_t *)destination;
    uint64_t done = 0;
    while (done < length) {
        uint64_t chunk = length - done;
        if (chunk > KUMO_VMO_TRANSFER_MAX) {
            chunk = KUMO_VMO_TRANSFER_MAX;
        }
        kumo_status_t status = kumo_status_from_x0(
            kumo_trap(KUMO_SYS_VMO_READ, vmo, offset + done,
                      (uint64_t)(uintptr_t)(dst + done), chunk, 0, 0)
                .x0);
        if (status != KUMO_OK) {
            return status;
        }
        done += chunk;
    }
    return KUMO_OK;
}

kumo_status_t kumo_vmo_write(kumo_handle_t vmo, uint64_t offset,
                             const void *source, uint64_t length)
{
    const uint8_t *src = (const uint8_t *)source;
    uint64_t done = 0;
    while (done < length) {
        uint64_t chunk = length - done;
        if (chunk > KUMO_VMO_TRANSFER_MAX) {
            chunk = KUMO_VMO_TRANSFER_MAX;
        }
        kumo_status_t status = kumo_status_from_x0(
            kumo_trap(KUMO_SYS_VMO_WRITE, vmo, offset + done,
                      (uint64_t)(uintptr_t)(src + done), chunk, 0, 0)
                .x0);
        if (status != KUMO_OK) {
            return status;
        }
        done += chunk;
    }
    return KUMO_OK;
}

struct kumo_status_value kumo_vmar_map(kumo_handle_t process, kumo_handle_t vmo,
                                       uint64_t vmo_offset, uint64_t virt,
                                       uint64_t length, uint64_t flags)
{
    struct kumo_trap_result result =
        kumo_trap(KUMO_SYS_VMAR_MAP, process, vmo, vmo_offset, virt, length,
                  flags);
    struct kumo_status_value value;
    value.status = kumo_status_from_x0(result.x0);
    value.value = result.x1;
    return value;
}

kumo_status_t kumo_vmar_unmap(kumo_handle_t process, uint64_t virt,
                              uint64_t length)
{
    return kumo_status_from_x0(
        kumo_trap(KUMO_SYS_VMAR_UNMAP, process, virt, length, 0, 0, 0).x0);
}

uint64_t kumo_process_create(uint64_t vmar_base, uint64_t vmar_size)
{
    return kumo_trap(KUMO_SYS_PROCESS_CREATE, vmar_base, vmar_size, 0, 0, 0,
                     0)
        .x0;
}

uint64_t kumo_address_space_create(kumo_handle_t process, uint64_t stack_virt,
                                   uint64_t stack_size)
{
    return kumo_trap(KUMO_SYS_ADDRESS_SPACE_CREATE, process, stack_virt,
                     stack_size, 0, 0, 0)
        .x0;
}

kumo_status_t kumo_process_run(kumo_handle_t process, uint64_t entry,
                               uint64_t stack, uint64_t arg, uint64_t arg2,
                               uint64_t flags)
{
    return kumo_status_from_x0(
        kumo_trap(KUMO_SYS_PROCESS_RUN, process, entry, stack, arg, arg2,
                  flags)
            .x0);
}

kumo_status_t kumo_process_wait(void)
{
    return kumo_status_from_x0(kumo_trap(KUMO_SYS_PROCESS_WAIT, 0, 0, 0, 0, 0,
                                         0)
                                   .x0);
}

uint64_t kumo_port_create(void)
{
    return kumo_trap(KUMO_SYS_PORT_CREATE, 0, 0, 0, 0, 0, 0).x0;
}

uint64_t kumo_port_wait(kumo_handle_t port)
{
    return kumo_trap(KUMO_SYS_PORT_WAIT, port, 0, 0, 0, 0, 0).x0;
}

kumo_status_t kumo_port_bind(kumo_handle_t port, kumo_handle_t object)
{
    return kumo_status_from_x0(
        kumo_trap(KUMO_SYS_PORT_BIND, port, object, 0, 0, 0, 0).x0);
}

kumo_status_t kumo_port_unbind(kumo_handle_t port, kumo_handle_t object)
{
    return kumo_status_from_x0(
        kumo_trap(KUMO_SYS_PORT_UNBIND, port, object, 0, 0, 0, 0).x0);
}

uint64_t kumo_clock_get(void)
{
    return kumo_trap(KUMO_SYS_CLOCK_GET, 0, 0, 0, 0, 0, 0).x0;
}
