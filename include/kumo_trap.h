/*
 * CIRRUS OS - KUMO / MUREX trap ABI
 *
 * aarch64 trap convention:
 *   x8     syscall number (enum kumo_syscall)
 *   x0-x5  arguments, in the order documented per prototype; unused are zero
 *   x0     primary result: status, handle, count, or byte count
 *   x1     secondary result: second handle, mapped address, next offset, or device address
 *
 * Status values are 32-bit signed values zero-extended in x0 by the kernel.
 * Use kumo_status_from_x0() to recover the signed value. Several calls use
 * KUMO_INVALID_U64 as a failure sentinel instead of a negative status; each
 * prototype says which convention applies.
 *
 * This header declares the C-side ABI. The typed functions are implemented by
 * the CIRRUS C runtime. kumo_trap() is the raw entry and is inline on aarch64 C.
 */
#ifndef CIRRUS_KUMO_TRAP_H
#define CIRRUS_KUMO_TRAP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KUMO_ABI_VERSION 3u
#define KUMO_SYSCALL_COUNT 56u

typedef uint32_t kumo_handle_t;
typedef uint64_t kumo_koid_t;
typedef int32_t kumo_status_t;

#define KUMO_INVALID_HANDLE ((kumo_handle_t)0u)
#define KUMO_INVALID_U64 UINT64_MAX

/* Syscall numbers. Six slots are reserved and have no kernel arm:
 * 2 HandleReplace, 6 ChannelCall, 9 ObjectWaitOne, 10 ObjectWaitMany,
 * 11 ObjectSignal, 16 VmarProtect. */
enum kumo_syscall {
    KUMO_SYS_HANDLE_CLOSE = 0,
    KUMO_SYS_HANDLE_DUPLICATE = 1,
    KUMO_SYS_HANDLE_REPLACE = 2,
    KUMO_SYS_CHANNEL_CREATE = 3,
    KUMO_SYS_CHANNEL_WRITE = 4,
    KUMO_SYS_CHANNEL_READ = 5,
    KUMO_SYS_CHANNEL_CALL = 6,
    KUMO_SYS_PORT_CREATE = 7,
    KUMO_SYS_PORT_WAIT = 8,
    KUMO_SYS_OBJECT_WAIT_ONE = 9,
    KUMO_SYS_OBJECT_WAIT_MANY = 10,
    KUMO_SYS_OBJECT_SIGNAL = 11,
    KUMO_SYS_VMO_CREATE = 12,
    KUMO_SYS_VMO_READ = 13,
    KUMO_SYS_VMO_WRITE = 14,
    KUMO_SYS_VMAR_MAP = 15,
    KUMO_SYS_VMAR_PROTECT = 16,
    KUMO_SYS_VMAR_UNMAP = 17,
    KUMO_SYS_PROCESS_CREATE = 18,
    KUMO_SYS_THREAD_CREATE = 19,
    KUMO_SYS_THREAD_START = 20,
    KUMO_SYS_PROCESS_EXIT = 21,
    KUMO_SYS_RESOURCE_MINT_MMIO = 22,
    KUMO_SYS_INTERRUPT_CREATE = 23,
    KUMO_SYS_INTERRUPT_WAIT = 24,
    KUMO_SYS_INTERRUPT_COMPLETE = 25,
    KUMO_SYS_CLOCK_GET = 26,
    KUMO_SYS_TIMER_CREATE = 27,
    KUMO_SYS_FUTEX_WAIT = 28,
    KUMO_SYS_FUTEX_WAKE = 29,
    KUMO_SYS_DEBUG_WRITE = 30,
    KUMO_SYS_ADDRESS_SPACE_CREATE = 31,
    KUMO_SYS_PROCESS_RUN = 32,
    KUMO_SYS_PORT_BIND = 33,
    KUMO_SYS_HANDLE_KOID = 34,
    KUMO_SYS_PROCESS_WAIT = 35,
    KUMO_SYS_RESOURCE_CREATE_CHILD = 36,
    KUMO_SYS_PORT_UNBIND = 37,
    KUMO_SYS_FRAMEBUFFER_CLAIM = 38,
    KUMO_SYS_IOMMU_FROM_RESOURCE = 39,
    KUMO_SYS_DEVICE_CTX_CREATE = 40,
    KUMO_SYS_DEVICE_VMAR_MAP = 41,
    KUMO_SYS_DEVICE_VMAR_UNMAP = 42,
    KUMO_SYS_DEVICE_CTX_WAIT_FAULT = 43,
    KUMO_SYS_DEVICE_CTX_INFO = 44,
    KUMO_SYS_VMO_CREATE_CONTIGUOUS = 45,
    KUMO_SYS_PROCESS_KILL = 46,
    KUMO_SYS_LOG_WRITE = 47,
    KUMO_SYS_LOG_READ = 48,
    KUMO_SYS_LOG_LEVEL = 49,
    KUMO_SYS_DEVICE_POWER_UP = 50,
    KUMO_SYS_DEVICE_POWER_STATUS = 51,
    KUMO_SYS_DEVICE_RESET_SET = 52,
    KUMO_SYS_DEVICE_REGISTER_WRITE = 53,
    KUMO_SYS_DEVICE_CTX_ACTIVATE = 54,
    KUMO_SYS_VMO_CREATE_PAGED = 55
};

enum kumo_errno {
    KUMO_OK = 0,
    KUMO_EINVAL = -1,
    KUMO_EBAD_HANDLE = -2,
    KUMO_EWRONG_TYPE = -3,
    KUMO_EACCES = -4,
    KUMO_ESHOULD_WAIT = -5,
    KUMO_EPEER_CLOSED = -6,
    KUMO_ENOMEM = -7,
    KUMO_ENOTSUP = -8,
    KUMO_EINTERNAL = -9,
    KUMO_ETIMEDOUT = -10,
    KUMO_EBAD_STATE = -11
};

/* Rights bits, mirroring kumo_abi::Rights. */
#define KUMO_RIGHT_DUPLICATE (1u << 0)
#define KUMO_RIGHT_TRANSFER  (1u << 1)
#define KUMO_RIGHT_READ      (1u << 2)
#define KUMO_RIGHT_WRITE     (1u << 3)
#define KUMO_RIGHT_MAP       (1u << 4)
#define KUMO_RIGHT_WAIT      (1u << 5)
#define KUMO_RIGHT_SIGNAL    (1u << 6)
#define KUMO_RIGHT_EXECUTE   (1u << 7)
#define KUMO_RIGHT_MANAGE    (1u << 8)
#define KUMO_RIGHTS_ALL      0x1ffu

/* Signal bits, mirroring kumo_abi::Signals. */
#define KUMO_SIGNAL_READABLE    (1u << 0)
#define KUMO_SIGNAL_WRITABLE    (1u << 1)
#define KUMO_SIGNAL_PEER_CLOSED (1u << 2)
#define KUMO_SIGNAL_TERMINATED  (1u << 3)
#define KUMO_SIGNAL_IRQ         (1u << 4)
#define KUMO_SIGNAL_TIMER       (1u << 5)
#define KUMO_SIGNAL_DEVICE_FAULT (1u << 6)

enum kumo_object_kind {
    KUMO_OBJECT_JOB = 1,
    KUMO_OBJECT_PROCESS = 2,
    KUMO_OBJECT_THREAD = 3,
    KUMO_OBJECT_CHANNEL = 4,
    KUMO_OBJECT_PORT = 5,
    KUMO_OBJECT_VMO = 6,
    KUMO_OBJECT_VMAR = 7,
    KUMO_OBJECT_EVENT = 8,
    KUMO_OBJECT_TIMER = 9,
    KUMO_OBJECT_RESOURCE = 10,
    KUMO_OBJECT_INTERRUPT = 11,
    KUMO_OBJECT_IOMMU = 12,
    KUMO_OBJECT_DEVICE_CTX = 13
};

/* vmar_map flags, mirroring kumo_abi::VmarFlags and kumo_hal::PageFlags. */
#define KUMO_VMAR_READ    (1ull << 0)
#define KUMO_VMAR_WRITE   (1ull << 1)
#define KUMO_VMAR_EXECUTE (1ull << 2)
#define KUMO_VMAR_USER    (1ull << 3)
#define KUMO_VMAR_DEVICE  (1ull << 4)
#define KUMO_VMAR_UNCACHED (1ull << 5)

/* process_run flags, mirroring kumo_abi::ProcessRunFlags. */
#define KUMO_PROCESS_RUN_ASYNC        (1ull << 0)
#define KUMO_PROCESS_RUN_TRANSFER_ARG (1ull << 1)
#define KUMO_PROCESS_RUN_TRANSFER_ARG2 (1ull << 2)

/* Kernel log levels, syslog numbering. */
#define KUMO_LOG_EMERG   0u
#define KUMO_LOG_ALERT   1u
#define KUMO_LOG_CRIT    2u
#define KUMO_LOG_ERR     3u
#define KUMO_LOG_WARNING 4u
#define KUMO_LOG_NOTICE  5u
#define KUMO_LOG_INFO    6u
#define KUMO_LOG_DEBUG   7u
#define KUMO_LOG_LEVEL_QUERY UINT64_MAX

/* Wire-format message header used by channels (kumo_ipc::MessageHeader). */
struct kumo_message_header {
    uint32_t ordinal;
    uint32_t flags;
    uint32_t bytes_len;
    uint32_t handles_len;
};
#define KUMO_MESSAGE_FLAG_EXPECTS_REPLY (1u << 0)
#define KUMO_MESSAGE_FLAG_IS_REPLY      (1u << 1)

#define KUMO_MAX_INLINE_BYTES    4096u
#define KUMO_MAX_MESSAGE_HANDLES 64u
#define KUMO_CHANNEL_WRITE_MAX   KUMO_MAX_INLINE_BYTES
#define KUMO_CHANNEL_READ_MAX    KUMO_MAX_INLINE_BYTES
#define KUMO_VMO_TRANSFER_MAX    256u
#define KUMO_DEBUG_WRITE_MAX     256u
#define KUMO_LOG_WRITE_MAX       256u

struct kumo_device_ctx_info {
    uint64_t fault_record;
    uint64_t fault_addr;
};

struct kumo_device_power_status {
    uint32_t version;
    uint32_t flags;
    uint32_t pmu[6];
    uint32_t cru[8];
};
#define KUMO_DEVICE_POWER_STATUS_VERSION 1u
#define KUMO_DEVICE_POWER_POWERED            (1u << 0)
#define KUMO_DEVICE_POWER_REPAIR_DONE        (1u << 1)
#define KUMO_DEVICE_POWER_BUS_IDLE           (1u << 2)
#define KUMO_DEVICE_POWER_BUS_IDLE_REQUESTED (1u << 3)
#define KUMO_DEVICE_POWER_CLOCK_AT_BOOT_RATE (1u << 4)
#define KUMO_DEVICE_POWER_APERTURE_MINTABLE  (1u << 5)

/* Raw trap result. x0 and x1 are exactly the registers the kernel writes. */
struct kumo_trap_result {
    uint64_t x0;
    uint64_t x1;
};
struct kumo_handles {
    uint64_t first;
    uint64_t second;
};
struct kumo_handle_value {
    uint64_t handle;
    uint64_t value;
};
struct kumo_status_value {
    kumo_status_t status;
    uint64_t value;
};

static inline kumo_status_t kumo_status_from_x0(uint64_t x0) {
    return (kumo_status_t)(int32_t)(uint32_t)x0;
}

#ifdef __cplusplus
#define KUMO_NORETURN [[noreturn]]
#else
#define KUMO_NORETURN _Noreturn
#endif

/*
 * Raw trap entry. On aarch64 C this is an inline svc #0. On other targets and
 * in C++ it is an external function provided by the CIRRUS C runtime.
 */
#if defined(__aarch64__) && !defined(__cplusplus) && (defined(__clang__) || defined(__GNUC__))
static inline struct kumo_trap_result
kumo_trap(uint64_t number, uint64_t a0, uint64_t a1, uint64_t a2,
          uint64_t a3, uint64_t a4, uint64_t a5)
{
    register uint64_t x0 __asm__("x0") = a0;
    register uint64_t x1 __asm__("x1") = a1;
    register uint64_t x2 __asm__("x2") = a2;
    register uint64_t x3 __asm__("x3") = a3;
    register uint64_t x4 __asm__("x4") = a4;
    register uint64_t x5 __asm__("x5") = a5;
    register uint64_t x8 __asm__("x8") = number;
    __asm__ volatile("svc #0"
                     : "+r"(x0), "+r"(x1)
                     : "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8)
                     : "memory");
    {
        struct kumo_trap_result result;
        result.x0 = x0;
        result.x1 = x1;
        return result;
    }
}
#else
struct kumo_trap_result kumo_trap(uint64_t number, uint64_t a0, uint64_t a1,
                                  uint64_t a2, uint64_t a3, uint64_t a4,
                                  uint64_t a5);
#endif

/* 0: x0 = status. */
kumo_status_t kumo_handle_close(kumo_handle_t handle);

/* 1: x0 = new handle, KUMO_INVALID_U64 on failure. rights is a rights mask. */
uint64_t kumo_handle_duplicate(kumo_handle_t handle, uint32_t rights);

/* 3: x0 = first handle, x1 = second handle; first is KUMO_INVALID_U64 on failure. */
struct kumo_handles kumo_channel_create(void);

/* 4: x0 = status; KUMO_INVALID_U64 if the message cannot be built.
 * transfer_handle is moved when nonzero. The kernel sends ordinal 1. */
kumo_status_t kumo_channel_write(kumo_handle_t channel, const void *bytes,
                                 uint64_t length, kumo_handle_t transfer_handle);

/* 5: x0 = bytes copied, x1 = first transferred handle or 0.
 * Blocks while the channel is empty; capacity is capped at KUMO_CHANNEL_READ_MAX. */
uint64_t kumo_channel_read(kumo_handle_t channel, void *buffer, uint64_t capacity,
                           uint64_t *handle_out);

/* 7: x0 = port handle, KUMO_INVALID_U64 on failure. */
uint64_t kumo_port_create(void);

/* 8: x0 = packet source koid, or 0. Blocks while the port is empty. */
uint64_t kumo_port_wait(kumo_handle_t port);

/* 12: x0 = VMO handle, KUMO_INVALID_U64 on failure. */
uint64_t kumo_vmo_create(uint64_t size);

/* 13: x0 = status; length is capped at KUMO_VMO_TRANSFER_MAX. */
kumo_status_t kumo_vmo_read(kumo_handle_t vmo, uint64_t offset, void *destination,
                            uint64_t length);

/* 14: x0 = status; an over-cap length is rejected with KUMO_EINVAL. */
kumo_status_t kumo_vmo_write(kumo_handle_t vmo, uint64_t offset, const void *source,
                             uint64_t length);

/* 15: x0 = status, x1 = chosen mapping address. x1 equals virt unless virt is 0. */
struct kumo_status_value kumo_vmar_map(kumo_handle_t process, kumo_handle_t vmo,
                                       uint64_t vmo_offset, uint64_t virt,
                                       uint64_t length, uint64_t flags);

/* 17: x0 = status. */
kumo_status_t kumo_vmar_unmap(kumo_handle_t process, uint64_t virt, uint64_t length);

/* 18: x0 = process handle, KUMO_INVALID_U64 on failure. The parent job is implicit. */
uint64_t kumo_process_create(uint64_t vmar_base, uint64_t vmar_size);

/* 19: x0 = thread handle, KUMO_INVALID_U64 on failure. */
uint64_t kumo_thread_create(kumo_handle_t process);

/* 20: x0 = status. */
kumo_status_t kumo_thread_start(kumo_handle_t thread, uint64_t entry, uint64_t stack,
                                uint64_t arg);

/* 21: does not return. */
KUMO_NORETURN void kumo_process_exit(uint64_t code);

/* 22: x0 = resource handle, KUMO_INVALID_U64 on failure. */
uint64_t kumo_resource_mint_mmio(kumo_handle_t resource, uint64_t phys_base,
                                 uint64_t length);

/* 23: x0 = interrupt handle, or a status value on failure. */
uint64_t kumo_interrupt_create(kumo_handle_t resource, uint32_t irq);

/* 24: x0 = fired count, or 0 on failure. Blocks until the interrupt fires. */
uint64_t kumo_interrupt_wait(kumo_handle_t interrupt);

/* 25: x0 = status. */
kumo_status_t kumo_interrupt_complete(kumo_handle_t interrupt);

/* 26: x0 = monotonic nanoseconds. */
uint64_t kumo_clock_get(void);

/* 27: x0 = timer handle, KUMO_INVALID_U64 on failure. delay_ns must be nonzero. */
uint64_t kumo_timer_create(uint64_t delay_ns);

/* 28: x0 = status. KUMO_ESHOULD_WAIT means the word changed; re-read and retry.
 * Only resident children may wait; Sora receives KUMO_ENOTSUP. */
kumo_status_t kumo_futex_wait(uint32_t *address, uint32_t expected);

/* 29: x0 = number woken, or a status value on failure. */
uint64_t kumo_futex_wake(uint32_t *address, uint32_t count);

/* 30: x0 = bytes written, KUMO_INVALID_U64 on failure.
 * length is capped at KUMO_DEBUG_WRITE_MAX. This is not a status return. */
uint64_t kumo_debug_write(const void *bytes, uint64_t length);

/* 31: x0 = address-space handle, KUMO_INVALID_U64 on failure. */
uint64_t kumo_address_space_create(kumo_handle_t process, uint64_t stack_virt,
                                   uint64_t stack_size);

/* 32: x0 = status. flags is a KUMO_PROCESS_RUN_* mask. */
kumo_status_t kumo_process_run(kumo_handle_t process, uint64_t entry, uint64_t stack,
                               uint64_t arg, uint64_t arg2, uint64_t flags);

/* 33: x0 = status. */
kumo_status_t kumo_port_bind(kumo_handle_t port, kumo_handle_t object);

/* 34: x0 = koid in the low 32 bits; the kernel truncates KoId to 32 bits here. */
uint64_t kumo_handle_koid(kumo_handle_t handle);

/* 35: x0 = status. Blocks until an asynchronous child exits. */
kumo_status_t kumo_process_wait(void);

/* 36: x0 = child resource handle, KUMO_INVALID_U64 on failure.
 * irq_base and irq_count are packed into one register by the wrapper. */
uint64_t kumo_resource_create_child(kumo_handle_t parent, uint64_t phys_base,
                                    uint64_t length, uint32_t irq_base,
                                    uint32_t irq_count);

/* 37: x0 = status. */
kumo_status_t kumo_port_unbind(kumo_handle_t port, kumo_handle_t object);

/* 38: x0 = status. */
kumo_status_t kumo_framebuffer_claim(kumo_handle_t resource, uint64_t phys_base,
                                     uint64_t length);

/* 39: x0 = IOMMU handle, or a status value on failure. kind is a kumo_abi IoMmuKind. */
uint64_t kumo_iommu_from_resource(kumo_handle_t resource, uint32_t kind,
                                  uint64_t phys_base, uint64_t length);

/* 40: x0 = device-context handle, or a status value on failure. */
uint64_t kumo_device_ctx_create(kumo_handle_t iommu, uint64_t stream_or_rid);

/* 41: x0 = status. rights is a KUMO_RIGHT_* mask. */
kumo_status_t kumo_device_vmar_map(kumo_handle_t ctx, kumo_handle_t vmo,
                                   uint64_t vmo_offset, uint64_t length,
                                   uint64_t iova_hint, uint32_t rights);

/* 42: x0 = status. */
kumo_status_t kumo_device_vmar_unmap(kumo_handle_t ctx, uint64_t iova, uint64_t length);

/* 43: x0 = status. Blocks on KUMO_ESHOULD_WAIT. */
kumo_status_t kumo_device_ctx_wait_fault(kumo_handle_t ctx);

/* 44: x0 = status; writes one struct kumo_device_ctx_info. */
kumo_status_t kumo_device_ctx_info(kumo_handle_t ctx, struct kumo_device_ctx_info *info);

/* 45: x0 = handle, x1 = physical base; x0 = KUMO_INVALID_U64 and x1 = 0 on failure. */
struct kumo_handle_value kumo_vmo_create_contiguous(uint64_t size);

/* 46: x0 = status. Requires MANAGE on the process handle. */
kumo_status_t kumo_process_kill(kumo_handle_t process);

/* 47: x0 = bytes written, KUMO_INVALID_U64 on failure.
 * level is KUMO_LOG_*; length is capped at KUMO_LOG_WRITE_MAX. */
uint64_t kumo_log_write(uint32_t level, const void *bytes, uint64_t length);

/* 48: x0 = bytes copied, x1 = next offset; KUMO_INVALID_U64 on failure.
 * Requires MANAGE on the root resource. */
uint64_t kumo_log_read(kumo_handle_t resource, uint64_t offset, void *buffer,
                       uint64_t length, uint64_t *next_offset_out);

/* 49: x0 = previous level, or KUMO_INVALID_U64 on failure.
 * Pass KUMO_LOG_LEVEL_QUERY as level to read without changing it. */
uint64_t kumo_log_level(kumo_handle_t resource, uint64_t level);

/* 50: x0 = status. */
kumo_status_t kumo_device_power_up(kumo_handle_t resource, uint64_t aperture_base);

/* 51: x0 = status; writes one struct kumo_device_power_status. */
kumo_status_t kumo_device_power_status(kumo_handle_t resource, uint64_t aperture_base,
                                       struct kumo_device_power_status *status);

/* 52: x0 = status. asserted is 0 or 1. */
kumo_status_t kumo_device_reset_set(kumo_handle_t resource, uint64_t aperture_base,
                                    uint64_t line, uint32_t asserted);

/* 53: x0 = status. */
kumo_status_t kumo_device_register_write(kumo_handle_t resource, uint64_t aperture_base,
                                         uint64_t index, uint64_t value);

/* 54: x0 = status. */
kumo_status_t kumo_device_ctx_activate(kumo_handle_t ctx);

/* 55: x0 = handle, KUMO_INVALID_U64 on failure.
 * The kernel writes one physical address per rounded-up 4 KiB page to
 * physical_pages, which must have page_count entries. */
uint64_t kumo_vmo_create_paged(uint64_t size, uint64_t *physical_pages,
                               uint64_t page_count);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#define KUMO_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define KUMO_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

KUMO_STATIC_ASSERT(KUMO_ABI_VERSION == 3u, "unexpected ABI version");
KUMO_STATIC_ASSERT(KUMO_SYSCALL_COUNT == 56u, "unexpected syscall count");
KUMO_STATIC_ASSERT(KUMO_SYS_HANDLE_CLOSE == 0, "syscall number drift");
KUMO_STATIC_ASSERT(KUMO_SYS_CHANNEL_CREATE == 3, "syscall number drift");
KUMO_STATIC_ASSERT(KUMO_SYS_PROCESS_EXIT == 21, "syscall number drift");
KUMO_STATIC_ASSERT(KUMO_SYS_DEBUG_WRITE == 30, "syscall number drift");
KUMO_STATIC_ASSERT(KUMO_SYS_VMO_CREATE_CONTIGUOUS == 45, "syscall number drift");
KUMO_STATIC_ASSERT(KUMO_SYS_VMO_CREATE_PAGED == 55, "syscall number drift");
KUMO_STATIC_ASSERT(sizeof(struct kumo_trap_result) == 16, "trap result size");
KUMO_STATIC_ASSERT(sizeof(struct kumo_message_header) == 16, "message header size");
KUMO_STATIC_ASSERT(offsetof(struct kumo_message_header, bytes_len) == 8,
                   "message header layout");
KUMO_STATIC_ASSERT(sizeof(struct kumo_device_ctx_info) == 16, "device ctx info size");
KUMO_STATIC_ASSERT(sizeof(struct kumo_device_power_status) == 64,
                   "device power status size");
KUMO_STATIC_ASSERT(offsetof(struct kumo_device_power_status, pmu) == 8,
                   "device power status layout");
KUMO_STATIC_ASSERT(offsetof(struct kumo_device_power_status, cru) == 32,
                   "device power status layout");
KUMO_STATIC_ASSERT(sizeof(struct kumo_handles) == 16, "handle pair size");

#endif /* CIRRUS_KUMO_TRAP_H */
