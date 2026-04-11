#ifndef KERNEL_INCLUDE_MM_H
#define KERNEL_INCLUDE_MM_H

#include "address-space.h"
#include "types.h"

#if !defined(__cplusplus)
#error "mm.h requires C++"
#endif

#if defined(ARCH_AARCH64) || defined(__aarch64__)
#include "mm/arch/aarch64/mm_backend.h"
#else
#include "mm/arch/aarch64/mm_backend.h"
#endif

#if defined(BOARD_RASPI3)
#include "mm/board/raspi3/mm_board.h"
#elif defined(BOARD_VIRT)
#include "mm/board/virt/mm_board.h"
#else
#include "mm/board/virt/mm_board.h"
#endif

#define KERNEL_PAGE_SIZE 0x1000ULL                  // 4096 bytes per page
#define KERNEL_USER_VA_BASE 0x0000000000000000ULL  // User virtual address base
#define KERNEL_USER_VA_LIMIT 0x0000FFFFFFFFFFFFULL // User virtual address limit
#define KERNEL_VA_BASE 0xFFFF000000000000ULL       // Kernel virtual address base

typedef enum PageFlags {
    PagePresent = 1ULL << 0,
    PageWritable = 1ULL << 1,
    PageExecutable = 1ULL << 2,
    PageUser = 1ULL << 3,
    PageDevice = 1ULL << 4,
    PageGlobal = 1ULL << 5,
    PageNoCache = 1ULL << 6
} PageFlags;

typedef struct VmMapping {
    VirtAddr virtual_base;
    PhysAddr physical_base;
    Size length;
    U64 flags;
} VmMapping;

typedef struct VmSelfTestResult {
    bool mmu_enabled;
    bool kernel_text_is_high;
    bool current_address_space_active;
    bool translation_roundtrip;
    PhysAddr process_address_space_root;
    PhysAddr kernel_address_space_root;
    PhysAddr kernel_text_physical;
    VirtAddr kernel_text_address;
} VmSelfTestResult;

namespace mm {

    constexpr U64 PageSize = KERNEL_PAGE_SIZE;
    constexpr VirtAddr UserVaBase = KERNEL_USER_VA_BASE;
    constexpr VirtAddr UserVaLimit = KERNEL_USER_VA_LIMIT;
    constexpr VirtAddr KernelVaBase = KERNEL_VA_BASE;

    enum class PageFlag : U64 {
        Present = PagePresent,
        Writable = PageWritable,
        Executable = PageExecutable,
        User = PageUser,
        Device = PageDevice,
        Global = PageGlobal,
        NoCache = PageNoCache,
    };

    constexpr U64 page_flag_bits(PageFlag flag) {
        return static_cast<U64>(flag);
    }

    class MemoryManager final {
    public:
        static Status bootstrap(void);
        static Status create_user_address_space(AddressSpace* address_space);
        static Status destroy_user_address_space(AddressSpace* address_space);
        static Status map(AddressSpace* address_space, const VmMapping* mapping);
        static Status unmap(AddressSpace* address_space, VirtAddr address, Size length);
        static Status switch_to(const AddressSpace* address_space);
        static Status init(void);
        static const AddressSpace* bootstrap_address_space(void);
        static const AddressSpace* current_address_space(void);
        static VirtAddr physical_to_kernel(PhysAddr address);
        static PhysAddr kernel_to_physical(VirtAddr address);
        static Status self_test(VmSelfTestResult* result);
        static bool is_user_address(VirtAddr address);
        static bool is_kernel_address(VirtAddr address);
    };

} // namespace mm

#endif // KERNEL_INCLUDE_MM_H
