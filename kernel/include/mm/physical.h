#ifndef KERNEL_INCLUDE_MM_PHYSICAL_H
#define KERNEL_INCLUDE_MM_PHYSICAL_H

#include "types.h"
#include "mm.h"

namespace mm {

    class PhysicalMemory final {
    public:
        // Initialize the physical page manager using the board-selected page pool.
        static Status init(void);

        // Allocate one physical page. Returns 0 on failure.
        static PhysAddr alloc_page(void);

        // Increment reference count for a physical page previously returned by alloc_page.
        static void retain_page(PhysAddr phys);

        // Release one reference to a physical page; when refcount hits zero the page is returned to free pool.
        static void free_page(PhysAddr phys);

        // Query helpers
        static unsigned int free_page_count(void);
        static unsigned int page_refcount(PhysAddr phys);
        static bool is_valid_phys_page(PhysAddr phys);
        // Reserve one physically contiguous low-address run for kernel-heap growth.
        // The heap still relies on the direct-map physical-to-kernel alias, so
        // its expansion path must consume pages from a stable contiguous runway.
        static PhysAddr reserve_contiguous_pages(unsigned int page_count);
        // Return the physical base address where the dedicated heap carve-out starts.
        // When the physical manager is not initialized this returns the board-configured value.
        static PhysAddr reserved_heap_physical_base(void);
    };

} // namespace mm

#endif // KERNEL_INCLUDE_MM_PHYSICAL_H
