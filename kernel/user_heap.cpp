#include "user_heap.h"

#include "heap.h"
#include "mm.h"
#include "process.h"

namespace {

    inline constexpr Size UserHeapAlignment = 16U;
    inline constexpr Size UserHeapDefaultRegionBytes = 256U * 1024U;
    /*
     * Keep the userspace heap above the fixed GUI/shared-view ranges.
     *
     * GUI window surfaces occupy a large reserved EL0 region starting at
     * 64 * 2 MiB, shared memory starts at 80 * 2 MiB, and shared input follows
     * the GUI surface range. The previous heap base sat inside the GUI surface
     * region, which makes larger GUI workloads eventually collide with raw
     * `SYS_MALLOC` mappings in the same process.
     */
    inline constexpr VirtAddr UserHeapBase = 256ULL * mm::backend::L2BlockSize;

    typedef struct UserHeapAllocation UserHeapAllocation;

    typedef struct UserHeapRegion {
        Process* owner_process;
        void* backing;
        PhysAddr physical_base;
        VirtAddr user_base;
        Size region_bytes;
        UserHeapAllocation* allocation_head;
        UserHeapAllocation* allocation_tail;
        UserHeapRegion* next;
        UserHeapRegion* prev;
    } UserHeapRegion;

    typedef struct UserHeapAllocation {
        UserHeapRegion* region;
        Size offset;
        Size size;
        UserHeapAllocation* next;
        UserHeapAllocation* prev;
    } UserHeapAllocation;

    UserHeapRegion* g_user_heap_region_head;
    UserHeapRegion* g_user_heap_region_tail;

    /**
     * Round one byte count up to the internal heap alignment.
     *
     * @param value Requested byte count.
     * @return Aligned byte count.
     */
    Size align_up_size(Size value) {
        const Size mask = UserHeapAlignment - 1U;

        return (value + mask) & ~mask;
    }

    /**
     * Round one arena length up to page granularity.
     *
     * The user heap no longer needs to burn a full 2 MiB arena on the first
     * small allocation because the user address-space mapper supports normal
     * page mappings. Keeping the arena page-sized preserves contiguous virtual
     * ranges while dramatically reducing per-process startup pressure.
     *
     * @param value Requested byte count.
     * @return Page-aligned region size.
     */
    Size align_up_region_bytes(Size value) {
        const Size mask = mm::PageSize - 1U;

        return (value + mask) & ~mask;
    }

    /**
     * Link one region into the global arena list.
     *
     * @param region Arena metadata to publish.
     * @return Nothing.
     */
    void link_region(UserHeapRegion* region) {
        if (region == NULL) {
            return;
        }

        region->prev = g_user_heap_region_tail;
        region->next = NULL;
        if (g_user_heap_region_tail != NULL) {
            g_user_heap_region_tail->next = region;
        }
        else {
            g_user_heap_region_head = region;
        }

        g_user_heap_region_tail = region;
    }

    /**
     * Unlink one region from the global arena list.
     *
     * @param region Arena metadata to remove.
     * @return Nothing.
     */
    void unlink_region(UserHeapRegion* region) {
        if (region == NULL) {
            return;
        }

        if (region->prev != NULL) {
            region->prev->next = region->next;
        }
        else {
            g_user_heap_region_head = region->next;
        }

        if (region->next != NULL) {
            region->next->prev = region->prev;
        }
        else {
            g_user_heap_region_tail = region->prev;
        }

        region->next = NULL;
        region->prev = NULL;
    }

    /**
     * Insert one allocation into its region in ascending-offset order.
     *
     * @param region Owning region.
     * @param allocation Allocation metadata to insert.
     * @return Nothing.
     */
    void link_allocation(UserHeapRegion* region, UserHeapAllocation* allocation) {
        UserHeapAllocation* cursor;

        if ((region == NULL) || (allocation == NULL)) {
            return;
        }

        cursor = region->allocation_head;
        while ((cursor != NULL) && (cursor->offset < allocation->offset)) {
            cursor = cursor->next;
        }

        if (cursor == NULL) {
            allocation->prev = region->allocation_tail;
            allocation->next = NULL;
            if (region->allocation_tail != NULL) {
                region->allocation_tail->next = allocation;
            }
            else {
                region->allocation_head = allocation;
            }

            region->allocation_tail = allocation;
            return;
        }

        allocation->next = cursor;
        allocation->prev = cursor->prev;
        if (cursor->prev != NULL) {
            cursor->prev->next = allocation;
        }
        else {
            region->allocation_head = allocation;
        }

        cursor->prev = allocation;
    }

    /**
     * Unlink one allocation from its owning region.
     *
     * @param allocation Allocation metadata to remove.
     * @return Nothing.
     */
    void unlink_allocation(UserHeapAllocation* allocation) {
        UserHeapRegion* region;

        if ((allocation == NULL) || (allocation->region == NULL)) {
            return;
        }

        region = allocation->region;
        if (allocation->prev != NULL) {
            allocation->prev->next = allocation->next;
        }
        else {
            region->allocation_head = allocation->next;
        }

        if (allocation->next != NULL) {
            allocation->next->prev = allocation->prev;
        }
        else {
            region->allocation_tail = allocation->prev;
        }

        allocation->next = NULL;
        allocation->prev = NULL;
        allocation->region = NULL;
    }

    /**
     * Pick the next free user virtual base for a process heap arena.
     *
     * @param process Owning process.
     * @return First unused arena base after the process's current heap regions.
     */
    VirtAddr next_region_base(const Process* process) {
        VirtAddr base = UserHeapBase;

        for (const UserHeapRegion* region = g_user_heap_region_head; region != NULL; region = region->next) {
            if (region->owner_process != process) {
                continue;
            }
            if ((region->user_base + region->region_bytes) > base) {
                base = region->user_base + region->region_bytes;
            }
        }

        return base;
    }

    /**
     * Find one free gap inside a region that can hold the requested size.
     *
     * @param region Arena being searched.
     * @param size Requested aligned payload bytes.
     * @param offset_out Receives the chosen region-relative offset.
     * @return StatusOK on success, or StatusNoSpace when the region is full.
     */
    Status find_gap(const UserHeapRegion* region, Size size, Size* offset_out) {
        Size candidate = 0U;

        if ((region == NULL) || (offset_out == NULL) || (size == 0U)) {
            return StatusInvalidArgument;
        }

        for (const UserHeapAllocation* allocation = region->allocation_head; allocation != NULL; allocation = allocation->next) {
            const Size aligned_candidate = align_up_size(candidate);

            if ((aligned_candidate + size) <= allocation->offset) {
                *offset_out = aligned_candidate;
                return StatusOK;
            }

            candidate = allocation->offset + allocation->size;
        }

        candidate = align_up_size(candidate);
        if ((candidate + size) > region->region_bytes) {
            return StatusNoSpace;
        }

        *offset_out = candidate;
        return StatusOK;
    }

    /**
     * Create and map one new heap arena into the target process.
     *
     * @param process Owning process.
     * @param region_out Receives the created region metadata.
     * @return StatusOK on success, or an allocation/mapping failure.
     */
    Status create_region(Process* process, Size minimum_bytes, UserHeapRegion** region_out) {
        UserHeapRegion* region;
        void* backing;
        VmMapping mapping;
        Size region_bytes;
        Status status;

        if ((process == NULL) || (region_out == NULL)) {
            return StatusInvalidArgument;
        }

        region_bytes = minimum_bytes > UserHeapDefaultRegionBytes ? align_up_region_bytes(minimum_bytes) : UserHeapDefaultRegionBytes;
        if (region_bytes == 0U) {
            return StatusInvalidArgument;
        }

        region = static_cast<UserHeapRegion*>(Heap::alloc(sizeof(UserHeapRegion), alignof(UserHeapRegion)));
        if (region == NULL) {
            return StatusNoMemory;
        }

        backing = Heap::alloc(region_bytes, mm::PageSize);
        if (backing == NULL) {
            Heap::free(region);
            return StatusNoMemory;
        }

        memzero(region, sizeof(*region));
        region->owner_process = process;
        region->backing = backing;
        region->physical_base = mm::MemoryManager::kernel_to_physical(reinterpret_cast<VirtAddr>(backing));
        region->user_base = next_region_base(process);
        region->region_bytes = region_bytes;

        mapping.virtual_base = region->user_base;
        mapping.physical_base = region->physical_base;
        mapping.length = region->region_bytes;
        mapping.flags = PagePresent | PageWritable | PageUser;
        status = mm::MemoryManager::map(&process->process_address_space, &mapping);
        if (status != StatusOK) {
            Heap::free(backing);
            Heap::free(region);
            return status;
        }

        link_region(region);
        *region_out = region;
        return StatusOK;
    }

    /**
     * Find one exact live allocation by user virtual base.
     *
     * @param process Owning process.
     * @param address User virtual address originally returned to EL0.
     * @return Matching allocation metadata, or NULL when not found.
     */
    UserHeapAllocation* find_allocation(Process* process, VirtAddr address) {
        for (UserHeapRegion* region = g_user_heap_region_head; region != NULL; region = region->next) {
            if (region->owner_process != process) {
                continue;
            }

            for (UserHeapAllocation* allocation = region->allocation_head; allocation != NULL; allocation = allocation->next) {
                if ((region->user_base + allocation->offset) == address) {
                    return allocation;
                }
            }
        }

        return NULL;
    }

    /**
     * Tear down one region that no longer owns any live allocations.
     *
     * @param region Empty region to release.
     * @return Nothing.
     */
    void destroy_empty_region(UserHeapRegion* region) {
        if ((region == NULL) || (region->allocation_head != NULL)) {
            return;
        }

        if ((region->owner_process != NULL) && (region->owner_process->process_address_space.page_table_root != 0U)) {
            (void)mm::MemoryManager::unmap(&region->owner_process->process_address_space, region->user_base, region->region_bytes);
        }

        unlink_region(region);
        Heap::free(region->backing);
        Heap::free(region);
    }

} // namespace

Status UserHeap::alloc_raw(Process* process, Size size, VirtAddr* address_out) {
    UserHeapRegion* region;
    UserHeapAllocation* allocation;
    Size aligned_size;
    Size offset;
    Status status;

    if ((process == NULL) || (address_out == NULL)) {
        return StatusInvalidArgument;
    }

    aligned_size = align_up_size(size == 0U ? 1U : size);

    for (region = g_user_heap_region_head; region != NULL; region = region->next) {
        if (region->owner_process != process) {
            continue;
        }

        status = find_gap(region, aligned_size, &offset);
        if (status == StatusOK) {
            break;
        }
    }

    if (region == NULL) {
        status = create_region(process, aligned_size, &region);
        if (status != StatusOK) {
            return status;
        }

        status = find_gap(region, aligned_size, &offset);
        if (status != StatusOK) {
            return status;
        }
    }

    allocation = static_cast<UserHeapAllocation*>(Heap::alloc(sizeof(UserHeapAllocation), alignof(UserHeapAllocation)));
    if (allocation == NULL) {
        return StatusNoMemory;
    }

    memzero(allocation, sizeof(*allocation));
    allocation->region = region;
    allocation->offset = offset;
    allocation->size = aligned_size;
    link_allocation(region, allocation);
    *address_out = region->user_base + offset;
    return StatusOK;
}

Status UserHeap::free_raw(Process* process, VirtAddr address) {
    UserHeapAllocation* allocation;
    UserHeapRegion* region;

    if (process == NULL) {
        return StatusInvalidArgument;
    }
    if (address == 0U) {
        return StatusOK;
    }

    allocation = find_allocation(process, address);
    if (allocation == NULL) {
        return StatusNotFound;
    }

    region = allocation->region;
    unlink_allocation(allocation);
    Heap::free(allocation);
    destroy_empty_region(region);
    return StatusOK;
}

Status UserHeap::release_process(Process* process) {
    UserHeapRegion* region;
    UserHeapRegion* next_region;

    if (process == NULL) {
        return StatusInvalidArgument;
    }

    for (region = g_user_heap_region_head; region != NULL; region = next_region) {
        UserHeapAllocation* allocation;
        UserHeapAllocation* next_allocation;

        next_region = region->next;
        if (region->owner_process != process) {
            continue;
        }

        for (allocation = region->allocation_head; allocation != NULL; allocation = next_allocation) {
            next_allocation = allocation->next;
            unlink_allocation(allocation);
            Heap::free(allocation);
        }

        if (process->process_address_space.page_table_root != 0U) {
            (void)mm::MemoryManager::unmap(&process->process_address_space, region->user_base, region->region_bytes);
        }

        unlink_region(region);
        Heap::free(region->backing);
        Heap::free(region);
    }

    return StatusOK;
}
