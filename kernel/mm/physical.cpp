#include "mm/physical.h"

#include "heap.h"
#include "mm.h"
#include "arch.h"

namespace mm {

    namespace {

        typedef struct PageFlags {
            bool allocated : 1;
            bool kernel_page : 1;
            unsigned int reserved : 30;
        } PageFlags;

        typedef struct Page {
            PhysAddr phys_addr;
            unsigned int ref_count;
            PageFlags flags;
            Page* next_page;
            Page* prev_page;
        } Page;

        typedef struct PageList {
            Page* head;
            Page* tail;
            unsigned int size;
        } PageList;

        static unsigned int num_pages = 0U;
        static Page* page_array = NULL;
        static PageList free_pages = { NULL, NULL, 0 };
        static bool initialized = false;
        static PhysAddr reserved_heap_base = 0ULL;
        static PhysAddr managed_phys_base = 0ULL;

        static inline void remove_page_list(PageList* list, Page* node) {
            if (!list || !node) return;
            if (node->prev_page) node->prev_page->next_page = node->next_page;
            else list->head = node->next_page;
            if (node->next_page) node->next_page->prev_page = node->prev_page;
            else list->tail = node->prev_page;
            if (list->size > 0) list->size -= 1;
            node->next_page = NULL;
            node->prev_page = NULL;
        }

        static inline void append_page_list(PageList* list, Page* node) {
            if (!list || !node) return;
            node->next_page = NULL;
            if (list->tail != NULL) {
                list->tail->next_page = node;
                node->prev_page = list->tail;
            }
            else {
                node->prev_page = NULL;
                list->head = node;
            }
            list->tail = node;
            list->size += 1;
        }

        static inline Page* pop_page_list(PageList* list) {
            if (!list || list->head == NULL) return NULL;
            Page* res = list->head;
            list->head = list->head->next_page;
            if (list->head != NULL) list->head->prev_page = NULL;
            list->size -= 1;
            if (list->head == NULL) list->tail = NULL;
            res->next_page = NULL;
            res->prev_page = NULL;
            return res;
        }

        static inline Page* pop_page_list_tail(PageList* list) {
            if (!list || list->tail == NULL) return NULL;
            Page* res = list->tail;

            list->tail = list->tail->prev_page;
            if (list->tail != NULL) {
                list->tail->next_page = NULL;
            }
            else {
                list->head = NULL;
            }

            list->size -= 1;
            res->next_page = NULL;
            res->prev_page = NULL;
            return res;
        }

        static inline unsigned int size_page_list(PageList* list) {
            return list ? list->size : 0U;
        }

        static inline Page* page_from_phys(PhysAddr phys_base, PhysAddr phys_addr) {
            if ((phys_addr < phys_base) || ((phys_addr - phys_base) >= (num_pages * PageSize))) {
                return NULL;
            }
            const unsigned long index = static_cast<unsigned long>((phys_addr - phys_base) / PageSize);
            return  &page_array[index];
        }

    } // namespace

    Status PhysicalMemory::init(void) {
        if (initialized) return StatusOK;

        if ((mm::board_config::PhysicalPagePoolBase == 0ULL) || (mm::board_config::PhysicalPagePoolSize < PageSize)) {
            return StatusNotSupported;
        }

        const PhysAddr phys_base = static_cast<PhysAddr>(mm::board_config::PhysicalPagePoolBase);
        const Size phys_size = static_cast<Size>(mm::board_config::PhysicalPagePoolSize);

        num_pages = static_cast<unsigned int>(phys_size / PageSize);
        if (num_pages == 0U) {
            return StatusNotSupported;
        }

        const Size page_array_bytes = static_cast<Size>(num_pages) * sizeof(Page);
        const unsigned int page_array_pages = static_cast<unsigned int>((page_array_bytes + PageSize - 1ULL) / PageSize);

        // Place the page metadata at the end of the managed physical-page pool.
        // The kernel heap growth path consumes low-address pages in order so it
        // can append a contiguous direct-map arena. Keeping the metadata out of
        // the low end preserves that runway.
        const PhysAddr page_array_phys = phys_base + phys_size - (static_cast<PhysAddr>(page_array_pages) * PageSize);
        const VirtAddr page_array_virt = MemoryManager::physical_to_kernel(page_array_phys);

        page_array = reinterpret_cast<Page*>(page_array_virt);
        memzero(page_array, page_array_bytes);

        // initialize free list
        free_pages.head = free_pages.tail = NULL;
        free_pages.size = 0;

        for (unsigned int i = 0U; i < num_pages; ++i) {
            page_array[i].phys_addr = phys_base + (static_cast<PhysAddr>(i) * PageSize);
            page_array[i].ref_count = 0U;
            page_array[i].flags.allocated = false;
            page_array[i].flags.kernel_page = false;
            page_array[i].next_page = NULL;
            page_array[i].prev_page = NULL;
            append_page_list(&free_pages, &page_array[i]);
        }

        // Mark the region used by the page array itself as allocated.
        if ((page_array_phys >= phys_base) && ((page_array_phys - phys_base) / PageSize < num_pages)) {
            unsigned long start_index = static_cast<unsigned long>((page_array_phys - phys_base) / PageSize);
            for (unsigned int j = 0U; j < page_array_pages; ++j) {
                const unsigned long idx = start_index + j;
                if (idx >= num_pages) break;
                Page* p = &page_array[idx];
                remove_page_list(&free_pages, p);
                p->flags.allocated = true;
                p->flags.kernel_page = true;
                p->ref_count = 1U;
            }
        }

        managed_phys_base = phys_base;
        reserved_heap_base = static_cast<PhysAddr>(mm::board_config::EarlyHeapPhysicalBase);
        initialized = true;
        return StatusOK;
    }

    PhysAddr PhysicalMemory::alloc_page(void) {
        if (!initialized) return 0U;

        const bool ints = arch::Arch::save_and_disable_interrupts();
        Page* page = pop_page_list_tail(&free_pages);
        if (page == NULL) {
            arch::Arch::restore_interrupts(ints);
            return 0U;
        }

        page->flags.kernel_page = true;
        page->flags.allocated = true;
        page->ref_count = 1U;

        PhysAddr phys = page->phys_addr;
        memzero(reinterpret_cast<void*>(MemoryManager::physical_to_kernel(phys)), PageSize);

        arch::Arch::restore_interrupts(ints);
        return phys;
    }

    PhysAddr PhysicalMemory::reserve_contiguous_pages(unsigned int page_count) {
        Page* cursor;
        PhysAddr expected_phys;
        PhysAddr base_phys;
        const bool ints = arch::Arch::save_and_disable_interrupts();

        if (!initialized || (page_count == 0U)) {
            arch::Arch::restore_interrupts(ints);
            return 0U;
        }
        if (free_pages.size < page_count) {
            arch::Arch::restore_interrupts(ints);
            return 0U;
        }

        cursor = free_pages.head;
        if (cursor == NULL) {
            arch::Arch::restore_interrupts(ints);
            return 0U;
        }

        base_phys = cursor->phys_addr;
        expected_phys = base_phys;
        for (unsigned int index = 0U; index < page_count; ++index) {
            if ((cursor == NULL) || (cursor->phys_addr != expected_phys)) {
                arch::Arch::restore_interrupts(ints);
                return 0U;
            }

            expected_phys += PageSize;
            cursor = cursor->next_page;
        }

        for (unsigned int index = 0U; index < page_count; ++index) {
            Page* page = pop_page_list(&free_pages);

            if (page == NULL) {
                arch::Arch::restore_interrupts(ints);
                return 0U;
            }

            page->flags.kernel_page = true;
            page->flags.allocated = true;
            page->ref_count = 1U;
            memzero(reinterpret_cast<void*>(MemoryManager::physical_to_kernel(page->phys_addr)), PageSize);
        }

        arch::Arch::restore_interrupts(ints);
        return base_phys;
    }

    void PhysicalMemory::retain_page(PhysAddr phys) {
        if (!initialized) return;
        Page* page = page_from_phys(managed_phys_base, phys);
        if (!page) return;
        page->flags.allocated = true;
        page->ref_count += 1U;
    }

    void PhysicalMemory::free_page(PhysAddr phys) {
        if (!initialized) return;

        Page* page = page_from_phys(managed_phys_base, phys & ~(PageSize - 1ULL));
        if (!page) return;

        if (page->ref_count == 0U) {
            // double free - ignore
            return;
        }

        page->ref_count -= 1U;
        if (page->ref_count > 0U) return;

        page->flags.allocated = false;
        page->flags.kernel_page = false;
        append_page_list(&free_pages, page);
    }

    unsigned int PhysicalMemory::free_page_count(void) {
        if (!initialized) return 0U;
        return size_page_list(&free_pages);
    }

    unsigned int PhysicalMemory::page_refcount(PhysAddr phys) {
        if (!initialized) return 0U;
        Page* page = page_from_phys(managed_phys_base, phys & ~(PageSize - 1ULL));
        if (!page) return 0U;
        return page->ref_count;
    }

    bool PhysicalMemory::is_valid_phys_page(PhysAddr phys) {
        if (!initialized) return false;
        return (phys >= managed_phys_base) && ((phys - managed_phys_base) < (static_cast<PhysAddr>(num_pages) * PageSize)) && ((phys & (PageSize - 1ULL)) == 0ULL);
    }

    PhysAddr PhysicalMemory::reserved_heap_physical_base(void) {
        if (initialized && (reserved_heap_base != 0ULL)) {
            return reserved_heap_base;
        }
        return static_cast<PhysAddr>(mm::board_config::EarlyHeapPhysicalBase);
    }

} // namespace mm
