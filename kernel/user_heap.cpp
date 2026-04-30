#include "user_heap.h"

#include "arch.h"
#include "debug-message.h"
#include "heap.h"
#include "mm.h"
#include "mm/physical.h"
#include "process.h"
#include "resource_manager.h"
#include "user_address_space_layout.h"

namespace {

    inline constexpr Size UserHeapAlignment = 16U;
    inline constexpr U64 DataAbortStatusCodeMask = 0x3fU;

    typedef struct UserHeapAllocation UserHeapAllocation;
    typedef struct UserHeapMappedPage UserHeapMappedPage;

    typedef struct UserHeapAllocation {
        Process* owner_process;
        VirtAddr user_base;
        Size size;
        UserHeapAllocation* next;
        UserHeapAllocation* prev;
    } UserHeapAllocation;

    typedef struct UserHeapMappedPage {
        Process* owner_process;
        VirtAddr user_page_base;
        PhysAddr physical_base;
        UserHeapMappedPage* next;
        UserHeapMappedPage* prev;
    } UserHeapMappedPage;

    typedef struct UserHeapRecordSlot {
        struct UserHeapRecordSlot* next_free;
    } UserHeapRecordSlot;

    typedef struct UserHeapRecordPageHeader {
        struct UserHeapRecordPageHeader* next_page;
        PhysAddr page_phys;
        U32 live_slots;
        U32 slot_capacity;
    } UserHeapRecordPageHeader;

    typedef struct UserHeapRecordPool {
        Size slot_bytes;
        Size slot_alignment;
        UserHeapRecordSlot* free_list;
        UserHeapRecordPageHeader* pages;
    } UserHeapRecordPool;

    UserHeapAllocation* g_user_heap_allocation_head;
    UserHeapAllocation* g_user_heap_allocation_tail;
    UserHeapMappedPage* g_user_heap_page_head;
    UserHeapMappedPage* g_user_heap_page_tail;
    UserHeapRecordPool g_user_heap_allocation_pool = {};
    UserHeapRecordPool g_user_heap_page_pool = {};

    /**
     * Round one fixed-record size up to the next aligned slot boundary.
     *
     * The user-heap metadata pools carve full physical pages into equal record
     * slots, so each slot stride must preserve the natural alignment of the
     * record type stored in the pool.
     *
     * @param value Raw record size in bytes.
     * @param alignment Required record alignment.
     * @return Aligned slot size.
     */
    constexpr Size align_up_record(Size value, Size alignment) {
        return (value + (alignment - 1U)) & ~(alignment - 1U);
    }

    /**
     * Initialize one user-heap metadata pool lazily.
     *
     * Allocation and mapped-page records only need backing after the first raw
     * heap allocation or page-fault population, so the pool geometry is filled
     * in on first use instead of at boot.
     *
     * @param pool Pool state to initialize.
     * @param record_bytes Raw record size.
     * @param record_alignment Natural record alignment.
     * @return Nothing.
     */
    void user_heap_record_pool_init(UserHeapRecordPool* pool, Size record_bytes, Size record_alignment) {
        if ((pool == NULL) || (record_bytes == 0U) || (record_alignment == 0U)) {
            return;
        }
        if (pool->slot_bytes != 0U) {
            return;
        }

        pool->slot_alignment = record_alignment;
        pool->slot_bytes = align_up_record(record_bytes, record_alignment);
        pool->free_list = NULL;
        pool->pages = NULL;
    }

    /**
     * Return the pool page header that owns one metadata slot.
     *
     * @param slot Slot pointer previously returned by a user-heap record pool.
     * @return Owning page header, or NULL for invalid input.
     */
    UserHeapRecordPageHeader* user_heap_record_page_from_slot(const void* slot) {
        if (slot == NULL) {
            return NULL;
        }

        return reinterpret_cast<UserHeapRecordPageHeader*>(
            reinterpret_cast<Uptr>(slot) & ~(static_cast<Uptr>(mm::PageSize) - 1U));
    }

    /**
     * Grow one metadata pool by carving one physical page into fixed slots.
     *
     * These records are long-lived kernel bookkeeping objects, so page-backed
     * pools keep them off the general heap while still allowing the whole page
     * to be reclaimed when every slot on that page becomes free.
     *
     * @param pool Pool to extend.
     * @return True when at least one new slot was added.
     */
    bool user_heap_record_pool_grow(UserHeapRecordPool* pool) {
        const PhysAddr page_phys = mm::PhysicalMemory::alloc_page();
        U8* page_base;
        UserHeapRecordPageHeader* page_header;
        Uptr slot_start;
        Size available_bytes;
        Size slot_capacity;

        if ((pool == NULL) || (pool->slot_bytes == 0U) || (pool->slot_alignment == 0U) || (page_phys == 0U)) {
            return false;
        }

        page_base = reinterpret_cast<U8*>(mm::MemoryManager::physical_to_kernel(page_phys));
        page_header = reinterpret_cast<UserHeapRecordPageHeader*>(page_base);
        memzero(page_header, sizeof(*page_header));
        page_header->next_page = pool->pages;
        page_header->page_phys = page_phys;
        pool->pages = page_header;

        slot_start = reinterpret_cast<Uptr>(page_base + sizeof(UserHeapRecordPageHeader));
        slot_start = align_up_record(slot_start, pool->slot_alignment);
        if (slot_start >= (reinterpret_cast<Uptr>(page_base) + mm::PageSize)) {
            pool->pages = page_header->next_page;
            mm::PhysicalMemory::free_page(page_phys);
            return false;
        }

        available_bytes = (reinterpret_cast<Uptr>(page_base) + mm::PageSize) - slot_start;
        slot_capacity = available_bytes / pool->slot_bytes;
        if (slot_capacity == 0U) {
            pool->pages = page_header->next_page;
            mm::PhysicalMemory::free_page(page_phys);
            return false;
        }

        page_header->slot_capacity = static_cast<U32>(slot_capacity);
        for (Size index = 0U; index < slot_capacity; ++index) {
            UserHeapRecordSlot* slot = reinterpret_cast<UserHeapRecordSlot*>(slot_start + (index * pool->slot_bytes));

            slot->next_free = pool->free_list;
            pool->free_list = slot;
        }

        return true;
    }

    /**
     * Allocate one metadata slot from the requested pool.
     *
     * @param pool Pool providing the record.
     * @return Zeroed record storage, or NULL on failure.
     */
    void* user_heap_record_pool_allocate(UserHeapRecordPool* pool) {
        UserHeapRecordSlot* slot;
        UserHeapRecordPageHeader* page_header;

        if ((pool == NULL) || (pool->slot_bytes == 0U)) {
            return NULL;
        }
        if ((pool->free_list == NULL) && !user_heap_record_pool_grow(pool)) {
            return NULL;
        }

        slot = pool->free_list;
        pool->free_list = slot->next_free;
        page_header = user_heap_record_page_from_slot(slot);
        if (page_header != NULL) {
            page_header->live_slots += 1U;
        }

        memzero(slot, pool->slot_bytes);
        return slot;
    }

    /**
     * Remove every free-list entry that belongs to one metadata page.
     *
     * @param pool Pool whose free list should be filtered.
     * @param page_header Page being reclaimed.
     * @return Nothing.
     */
    void user_heap_record_pool_remove_page_slots(UserHeapRecordPool* pool, UserHeapRecordPageHeader* page_header) {
        const Uptr page_start = reinterpret_cast<Uptr>(page_header);
        const Uptr page_limit = page_start + mm::PageSize;
        UserHeapRecordSlot* previous = NULL;
        UserHeapRecordSlot* slot;

        if ((pool == NULL) || (page_header == NULL)) {
            return;
        }

        slot = pool->free_list;
        while (slot != NULL) {
            UserHeapRecordSlot* next_slot = slot->next_free;
            const Uptr slot_address = reinterpret_cast<Uptr>(slot);

            if ((slot_address >= page_start) && (slot_address < page_limit)) {
                if (previous != NULL) {
                    previous->next_free = next_slot;
                }
                else {
                    pool->free_list = next_slot;
                }
            }
            else {
                previous = slot;
            }

            slot = next_slot;
        }
    }

    /**
     * Unlink one reclaimed page from the pool page list.
     *
     * @param pool Pool owning the page.
     * @param page_header Page header to unlink.
     * @return Nothing.
     */
    void user_heap_record_pool_unlink_page(UserHeapRecordPool* pool, UserHeapRecordPageHeader* page_header) {
        UserHeapRecordPageHeader* previous = NULL;
        UserHeapRecordPageHeader* cursor;

        if ((pool == NULL) || (page_header == NULL)) {
            return;
        }

        cursor = pool->pages;
        while ((cursor != NULL) && (cursor != page_header)) {
            previous = cursor;
            cursor = cursor->next_page;
        }
        if (cursor == NULL) {
            return;
        }

        if (previous != NULL) {
            previous->next_page = cursor->next_page;
        }
        else {
            pool->pages = cursor->next_page;
        }
    }

    /**
     * Return one metadata record to its page-backed pool.
     *
     * @param pool Pool receiving the record back.
     * @param record Record previously allocated from the pool.
     * @return Nothing.
     */
    void user_heap_record_pool_free(UserHeapRecordPool* pool, void* record) {
        UserHeapRecordSlot* slot;
        UserHeapRecordPageHeader* page_header;

        if ((pool == NULL) || (record == NULL)) {
            return;
        }

        slot = static_cast<UserHeapRecordSlot*>(record);
        page_header = user_heap_record_page_from_slot(record);
        slot->next_free = pool->free_list;
        pool->free_list = slot;

        if ((page_header == NULL) || (page_header->live_slots == 0U)) {
            return;
        }

        page_header->live_slots -= 1U;
        if (page_header->live_slots != 0U) {
            return;
        }

        user_heap_record_pool_remove_page_slots(pool, page_header);
        user_heap_record_pool_unlink_page(pool, page_header);
        mm::PhysicalMemory::free_page(page_header->page_phys);
    }

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
     * Round one byte count up to page granularity.
     *
     * @param value Requested byte count.
     * @return Page-aligned byte count.
     */
    Size align_up_page(Size value) {
        const Size mask = mm::PageSize - 1U;

        return (value + mask) & ~mask;
    }

    /**
     * Round one user virtual address up to heap alignment.
     *
     * @param address Candidate user virtual address.
     * @return Heap-aligned user virtual address.
     */
    VirtAddr align_up_address(VirtAddr address) {
        const VirtAddr mask = static_cast<VirtAddr>(UserHeapAlignment - 1U);

        return (address + mask) & ~mask;
    }

    /**
     * Allocate one raw-allocation metadata record from the page-backed pool.
     *
     * @param process Owning process for diagnostics.
     * @return Allocation record, or NULL on failure.
     */
    UserHeapAllocation* allocate_user_heap_allocation_record(Process* process) {
        UserHeapAllocation* allocation;
        Status status;
        const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

        user_heap_record_pool_init(&g_user_heap_allocation_pool, sizeof(UserHeapAllocation), alignof(UserHeapAllocation));
        allocation = static_cast<UserHeapAllocation*>(user_heap_record_pool_allocate(&g_user_heap_allocation_pool));
        arch::Arch::restore_interrupts(interrupts_enabled);
        if (allocation == NULL) {
            return NULL;
        }

        status = KernelResourceManager::track_external(
            KernelResourceKind::UserHeapAllocationRecord,
            allocation,
            g_user_heap_allocation_pool.slot_bytes,
            (process != NULL) ? process->id : 0ULL,
            "user-heap-allocation");
        if (status != StatusOK) {
            const bool rollback_interrupts = arch::Arch::save_and_disable_interrupts();

            user_heap_record_pool_free(&g_user_heap_allocation_pool, allocation);
            arch::Arch::restore_interrupts(rollback_interrupts);
            return NULL;
        }

        return allocation;
    }

    /**
     * Return one raw-allocation metadata record to the page-backed pool.
     *
     * @param allocation Record to release.
     * @return Nothing.
     */
    void free_user_heap_allocation_record(UserHeapAllocation* allocation) {
        const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

        (void)KernelResourceManager::untrack(KernelResourceKind::UserHeapAllocationRecord, allocation);
        user_heap_record_pool_free(&g_user_heap_allocation_pool, allocation);
        arch::Arch::restore_interrupts(interrupts_enabled);
    }

    /**
     * Allocate one mapped-page metadata record from the page-backed pool.
     *
     * @param process Owning process for diagnostics.
     * @return Page record, or NULL on failure.
     */
    UserHeapMappedPage* allocate_user_heap_page_record(Process* process) {
        UserHeapMappedPage* page;
        Status status;
        const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

        user_heap_record_pool_init(&g_user_heap_page_pool, sizeof(UserHeapMappedPage), alignof(UserHeapMappedPage));
        page = static_cast<UserHeapMappedPage*>(user_heap_record_pool_allocate(&g_user_heap_page_pool));
        arch::Arch::restore_interrupts(interrupts_enabled);
        if (page == NULL) {
            return NULL;
        }

        status = KernelResourceManager::track_external(
            KernelResourceKind::UserHeapMappedPageRecord,
            page,
            g_user_heap_page_pool.slot_bytes,
            (process != NULL) ? process->id : 0ULL,
            "user-heap-page");
        if (status != StatusOK) {
            const bool rollback_interrupts = arch::Arch::save_and_disable_interrupts();

            user_heap_record_pool_free(&g_user_heap_page_pool, page);
            arch::Arch::restore_interrupts(rollback_interrupts);
            return NULL;
        }

        return page;
    }

    /**
     * Return one mapped-page metadata record to the page-backed pool.
     *
     * @param page Record to release.
     * @return Nothing.
     */
    void free_user_heap_page_record(UserHeapMappedPage* page) {
        const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

        (void)KernelResourceManager::untrack(KernelResourceKind::UserHeapMappedPageRecord, page);
        user_heap_record_pool_free(&g_user_heap_page_pool, page);
        arch::Arch::restore_interrupts(interrupts_enabled);
    }

    /**
     * Link one allocation into the global registry sorted by user address.
     *
     * Sorting by address keeps per-process gap searches deterministic even
     * though allocations for different processes share one global list.
     *
     * @param allocation Allocation metadata to publish.
     * @return Nothing.
     */
    void link_allocation(UserHeapAllocation* allocation) {
        UserHeapAllocation* cursor;

        if (allocation == NULL) {
            return;
        }

        cursor = g_user_heap_allocation_head;
        while ((cursor != NULL) && (cursor->user_base <= allocation->user_base)) {
            cursor = cursor->next;
        }

        if (cursor == NULL) {
            allocation->prev = g_user_heap_allocation_tail;
            allocation->next = NULL;
            if (g_user_heap_allocation_tail != NULL) {
                g_user_heap_allocation_tail->next = allocation;
            }
            else {
                g_user_heap_allocation_head = allocation;
            }

            g_user_heap_allocation_tail = allocation;
            return;
        }

        allocation->next = cursor;
        allocation->prev = cursor->prev;
        if (cursor->prev != NULL) {
            cursor->prev->next = allocation;
        }
        else {
            g_user_heap_allocation_head = allocation;
        }

        cursor->prev = allocation;
    }

    /**
     * Unlink one allocation from the global registry.
     *
     * @param allocation Allocation metadata to remove.
     * @return Nothing.
     */
    void unlink_allocation(UserHeapAllocation* allocation) {
        if (allocation == NULL) {
            return;
        }

        if (allocation->prev != NULL) {
            allocation->prev->next = allocation->next;
        }
        else {
            g_user_heap_allocation_head = allocation->next;
        }

        if (allocation->next != NULL) {
            allocation->next->prev = allocation->prev;
        }
        else {
            g_user_heap_allocation_tail = allocation->prev;
        }

        allocation->next = NULL;
        allocation->prev = NULL;
    }

    /**
     * Link one mapped heap page into the global page registry.
     *
     * @param page Page metadata to publish.
     * @return Nothing.
     */
    void link_page(UserHeapMappedPage* page) {
        if (page == NULL) {
            return;
        }

        page->prev = g_user_heap_page_tail;
        page->next = NULL;
        if (g_user_heap_page_tail != NULL) {
            g_user_heap_page_tail->next = page;
        }
        else {
            g_user_heap_page_head = page;
        }

        g_user_heap_page_tail = page;
    }

    /**
     * Unlink one mapped heap page from the global page registry.
     *
     * @param page Page metadata to remove.
     * @return Nothing.
     */
    void unlink_page(UserHeapMappedPage* page) {
        if (page == NULL) {
            return;
        }

        if (page->prev != NULL) {
            page->prev->next = page->next;
        }
        else {
            g_user_heap_page_head = page->next;
        }

        if (page->next != NULL) {
            page->next->prev = page->prev;
        }
        else {
            g_user_heap_page_tail = page->prev;
        }

        page->next = NULL;
        page->prev = NULL;
    }

    /**
     * Report whether one process exposes a valid sparse heap reservation.
     *
     * @param process Candidate process.
     * @return True when the process has a usable heap reservation window.
     */
    bool process_has_heap_reservation(const Process* process) {
        return (process != NULL)
            && (process->user_heap_base != 0U)
            && (process->user_heap_reserved_bytes != 0U);
    }

    /**
     * Report whether one address lies inside the process heap reservation.
     *
     * @param process Owning process.
     * @param address Candidate user virtual address.
     * @return True when the address falls inside the reserved heap window.
     */
    bool heap_contains_address(const Process* process, VirtAddr address) {
        const VirtAddr limit = process->user_heap_base + process->user_heap_reserved_bytes;

        return process_has_heap_reservation(process)
            && (address >= process->user_heap_base)
            && (address < limit);
    }

    /**
     * Report whether one address falls inside the shared in-process heap half.
     *
     * The lower half of the reserved EL0 heap window belongs to the shared
     * userspace allocator compiled into ros_support and the GUI DLLs. Any first
     * touch inside that half may legitimately need a page.
     *
     * @param process Owning process.
     * @param address Candidate user virtual address.
     * @return True when the address lies inside the shared heap half.
     */
    bool shared_allocator_contains_address(const Process* process, VirtAddr address) {
        return process_has_heap_reservation(process)
            && (address >= process->user_heap_base)
            && (address < user_address_space::HeapCompatBase);
    }

    /**
     * Find one exact live allocation by its starting address.
     *
     * @param process Owning process.
     * @param address User virtual address originally returned to EL0.
     * @return Matching allocation metadata, or NULL when not found.
     */
    UserHeapAllocation* find_allocation(Process* process, VirtAddr address) {
        for (UserHeapAllocation* allocation = g_user_heap_allocation_head; allocation != NULL; allocation = allocation->next) {
            if ((allocation->owner_process == process) && (allocation->user_base == address)) {
                return allocation;
            }
        }

        return NULL;
    }

    /**
     * Find one compatibility allocation that covers a faulting address.
     *
     * The upper half of the reserved heap window still exists for rare callers
     * that explicitly use the kernel raw heap ABI. Those addresses must match a
     * live kernel allocation before the fault path will populate pages there.
     *
     * @param process Owning process.
     * @param address Faulting or copy target address.
     * @return Matching allocation, or NULL when the address is outside live raw allocations.
     */
    UserHeapAllocation* find_allocation_covering(Process* process, VirtAddr address) {
        for (UserHeapAllocation* allocation = g_user_heap_allocation_head; allocation != NULL; allocation = allocation->next) {
            const VirtAddr allocation_limit = allocation->user_base + allocation->size;

            if (allocation->owner_process != process) {
                continue;
            }
            if ((address >= allocation->user_base) && (address < allocation_limit)) {
                return allocation;
            }
        }

        return NULL;
    }

    /**
     * Find one mapped heap page record by process and user page base.
     *
     * @param process Owning process.
     * @param user_page_base Page-aligned user virtual address.
     * @return Matching page record, or NULL when the page is not mapped.
     */
    UserHeapMappedPage* find_page(Process* process, VirtAddr user_page_base) {
        for (UserHeapMappedPage* page = g_user_heap_page_head; page != NULL; page = page->next) {
            if ((page->owner_process == process) && (page->user_page_base == user_page_base)) {
                return page;
            }
        }

        return NULL;
    }

    /**
     * Report whether any live allocation still touches one heap page.
     *
     * @param process Owning process.
     * @param user_page_base Page-aligned user virtual address.
     * @return True when at least one allocation overlaps the page.
     */
    bool page_has_live_allocation(Process* process, VirtAddr user_page_base) {
        const VirtAddr page_limit = user_page_base + mm::PageSize;

        for (UserHeapAllocation* allocation = g_user_heap_allocation_head; allocation != NULL; allocation = allocation->next) {
            const VirtAddr allocation_limit = allocation->user_base + allocation->size;

            if (allocation->owner_process != process) {
                continue;
            }
            if ((allocation->user_base < page_limit) && (allocation_limit > user_page_base)) {
                return true;
            }
        }

        return false;
    }

    /**
     * Choose one free gap inside the reserved heap window.
     *
     * @param process Owning process.
     * @param size Requested aligned payload bytes.
     * @param address_out Receives the chosen user virtual base.
     * @return StatusOK on success, or StatusNoSpace when the reservation is full.
     */
    Status find_gap(Process* process, Size size, VirtAddr* address_out) {
        VirtAddr candidate;
        VirtAddr reservation_limit;

        if ((process == NULL) || (address_out == NULL) || (size == 0U) || !process_has_heap_reservation(process)) {
            return StatusInvalidArgument;
        }

        candidate = user_address_space::HeapCompatBase;
        reservation_limit = process->user_heap_base + process->user_heap_reserved_bytes;
        for (UserHeapAllocation* allocation = g_user_heap_allocation_head; allocation != NULL; allocation = allocation->next) {
            VirtAddr aligned_candidate;

            if (allocation->owner_process != process) {
                continue;
            }

            aligned_candidate = align_up_address(candidate);
            if ((aligned_candidate + size) <= allocation->user_base) {
                *address_out = aligned_candidate;
                return StatusOK;
            }

            candidate = allocation->user_base + allocation->size;
        }

        candidate = align_up_address(candidate);
        if ((candidate + size) > reservation_limit) {
            return StatusNoSpace;
        }

        *address_out = candidate;
        return StatusOK;
    }

    /**
     * Map one heap page into the target process on demand.
     *
     * @param process Owning process.
     * @param user_page_base Page-aligned user virtual address to populate.
     * @return StatusOK on success, or an allocation/mapping failure.
     */
    Status map_heap_page(Process* process, VirtAddr user_page_base) {
        UserHeapMappedPage* page;
        VmMapping mapping;
        PhysAddr physical_page;
        Status status;

        if ((process == NULL) || !heap_contains_address(process, user_page_base)) {
            return StatusInvalidArgument;
        }
        if (find_page(process, user_page_base) != NULL) {
            return StatusOK;
        }

        physical_page = mm::PhysicalMemory::alloc_page();
        if (physical_page == 0U) {
            KERROR("[user-heap] page fault map failed pid=%llu page=0x%llx reason=no-physical-page\n",
                static_cast<unsigned long long>(process->id),
                static_cast<unsigned long long>(user_page_base));
            return StatusNoMemory;
        }

        mapping.virtual_base = user_page_base;
        mapping.physical_base = physical_page;
        mapping.length = mm::PageSize;
        mapping.flags = PagePresent | PageWritable | PageUser;
        status = mm::MemoryManager::map(&process->process_address_space, &mapping);
        if (status != StatusOK) {
            KERROR("[user-heap] page fault map failed pid=%llu page=0x%llx status=%lld reason=mm-map\n",
                static_cast<unsigned long long>(process->id),
                static_cast<unsigned long long>(user_page_base),
                static_cast<long long>(status));
            mm::PhysicalMemory::free_page(physical_page);
            return status;
        }

        page = allocate_user_heap_page_record(process);
        if (page == NULL) {
            KERROR("[user-heap] page fault map failed pid=%llu page=0x%llx reason=no-page-metadata\n",
                static_cast<unsigned long long>(process->id),
                static_cast<unsigned long long>(user_page_base));
            (void)mm::MemoryManager::unmap(&process->process_address_space, user_page_base, mm::PageSize);
            mm::PhysicalMemory::free_page(physical_page);
            return StatusNoMemory;
        }

        memzero(page, sizeof(*page));
        page->owner_process = process;
        page->user_page_base = user_page_base;
        page->physical_base = physical_page;
        link_page(page);
        return StatusOK;
    }

    /**
     * Release one mapped heap page and return its physical backing.
     *
     * @param page Page metadata to release.
     * @param process_address_space_alive True when the process can still unmap.
     * @return Nothing.
     */
    void release_heap_page(UserHeapMappedPage* page, bool process_address_space_alive) {
        if (page == NULL) {
            return;
        }

        if (process_address_space_alive
            && (page->owner_process != NULL)
            && (page->owner_process->process_address_space.page_table_root != 0U)) {
            (void)mm::MemoryManager::unmap(&page->owner_process->process_address_space, page->user_page_base, mm::PageSize);
        }

        mm::PhysicalMemory::free_page(page->physical_base);
        unlink_page(page);
        free_user_heap_page_record(page);
    }

    /**
     * Report whether one ESR can be satisfied by installing an EL0 heap page.
     *
     * Fresh user address spaces still inherit the kernel's low-GiB identity map
     * while the scheduler performs TTBR0 handoff on kernel stacks. First-touch
     * heap accesses can therefore arrive either as translation faults or as
     * permission faults against those inherited kernel-only block mappings.
     * Both cases are recoverable for addresses inside the reserved user heap.
     *
     * @param esr Raw ESR_EL1 value.
     * @return True when the fault can be resolved by installing one heap page.
     */
    bool is_heap_population_fault(U64 esr) {
        switch (esr & DataAbortStatusCodeMask) {
        case 0x04U:
        case 0x05U:
        case 0x06U:
        case 0x07U:
        case 0x0dU:
        case 0x0eU:
        case 0x0fU:
            return true;
        default:
            return false;
        }
    }

} // namespace

Status UserHeap::alloc_raw(Process* process, Size size, VirtAddr* address_out) {
    UserHeapAllocation* allocation;
    Size aligned_size;
    VirtAddr allocation_base;
    Status status;

    if ((process == NULL) || (address_out == NULL)) {
        return StatusInvalidArgument;
    }
    if (!process_has_heap_reservation(process)) {
        return StatusNotSupported;
    }

    aligned_size = align_up_size(size == 0U ? 1U : size);
    status = find_gap(process, aligned_size, &allocation_base);
    if (status != StatusOK) {
        return status;
    }

    allocation = allocate_user_heap_allocation_record(process);
    if (allocation == NULL) {
        return StatusNoMemory;
    }

    memzero(allocation, sizeof(*allocation));
    allocation->owner_process = process;
    allocation->user_base = allocation_base;
    allocation->size = aligned_size;
    link_allocation(allocation);
    *address_out = allocation_base;
    return StatusOK;
}

Status UserHeap::free_raw(Process* process, VirtAddr address) {
    UserHeapAllocation* allocation;
    VirtAddr release_base;
    VirtAddr release_limit;

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

    release_base = allocation->user_base & ~(mm::PageSize - 1U);
    release_limit = align_up_page(allocation->user_base + allocation->size);
    unlink_allocation(allocation);
    free_user_heap_allocation_record(allocation);

    for (VirtAddr page_base = release_base; page_base < release_limit; page_base += mm::PageSize) {
        UserHeapMappedPage* page;

        if (page_has_live_allocation(process, page_base)) {
            continue;
        }

        page = find_page(process, page_base);
        if (page != NULL) {
            release_heap_page(page, true);
        }
    }

    return StatusOK;
}

Status UserHeap::handle_page_fault(Process* process, VirtAddr address, U64 esr) {
    VirtAddr page_base;
    Status status;

    if ((process == NULL) || !process_has_heap_reservation(process)) {
        return StatusInvalidArgument;
    }
    if (!is_heap_population_fault(esr)) {
        return StatusNotSupported;
    }
    if (!heap_contains_address(process, address)) {
        return StatusNotFound;
    }
    if (!shared_allocator_contains_address(process, address) && (find_allocation_covering(process, address) == NULL)) {
        return StatusNotFound;
    }

    page_base = address & ~(mm::PageSize - 1U);
    status = map_heap_page(process, page_base);
    if (status != StatusOK) {
        KERROR("[user-heap] fault recovery rejected pid=%llu far=0x%llx page=0x%llx esr=0x%llx status=%lld\n",
            static_cast<unsigned long long>(process->id),
            static_cast<unsigned long long>(address),
            static_cast<unsigned long long>(page_base),
            static_cast<unsigned long long>(esr),
            static_cast<long long>(status));
    }

    return status;
}

Status UserHeap::ensure_range_mapped(Process* process, VirtAddr address, Size size) {
    VirtAddr page_base;
    VirtAddr range_limit;

    if (process == NULL) {
        return StatusInvalidArgument;
    }
    if (size == 0U) {
        return StatusOK;
    }

    page_base = address & ~(mm::PageSize - 1U);
    range_limit = align_up_page(address + size);
    for (; page_base < range_limit; page_base += mm::PageSize) {
        Status status;

        if (!heap_contains_address(process, page_base)) {
            continue;
        }
        if (!shared_allocator_contains_address(process, page_base)
            && !page_has_live_allocation(process, page_base)) {
            return StatusNotFound;
        }

        status = map_heap_page(process, page_base);
        if (status != StatusOK) {
            KERROR("[user-heap] ensure-range failed pid=%llu page=0x%llx start=0x%llx size=%llu status=%lld\n",
                static_cast<unsigned long long>(process->id),
                static_cast<unsigned long long>(page_base),
                static_cast<unsigned long long>(address),
                static_cast<unsigned long long>(size),
                static_cast<long long>(status));
            return status;
        }
    }

    return StatusOK;
}

Size UserHeap::mapped_heap_bytes(Process* process) {
    Size mapped_bytes = 0U;
    const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

    if (process == NULL) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return 0U;
    }

    for (UserHeapMappedPage* page = g_user_heap_page_head; page != NULL; page = page->next) {
        if (page->owner_process == process) {
            mapped_bytes += mm::PageSize;
        }
    }

    arch::Arch::restore_interrupts(interrupts_enabled);
    return mapped_bytes;
}

Status UserHeap::release_process(Process* process) {
    UserHeapAllocation* allocation;
    UserHeapAllocation* next_allocation;
    UserHeapMappedPage* page;
    UserHeapMappedPage* next_page;

    if (process == NULL) {
        return StatusInvalidArgument;
    }

    for (allocation = g_user_heap_allocation_head; allocation != NULL; allocation = next_allocation) {
        next_allocation = allocation->next;
        if (allocation->owner_process != process) {
            continue;
        }

        unlink_allocation(allocation);
        free_user_heap_allocation_record(allocation);
    }

    for (page = g_user_heap_page_head; page != NULL; page = next_page) {
        next_page = page->next;
        if (page->owner_process != process) {
            continue;
        }

        release_heap_page(page, process->process_address_space.page_table_root != 0U);
    }

    process->user_heap_base = 0U;
    process->user_heap_reserved_bytes = 0U;

    return StatusOK;
}
