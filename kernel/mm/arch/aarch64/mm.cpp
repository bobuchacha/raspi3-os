#include "mm.h"

#include "debug-message.h"
#include "heap.h"
#include "internal/mm/address_space.h"

extern "C" void kernel_entry(void);

namespace mm {

    namespace {

#define MMU_MAP_TRACE(fmt, ...) KDEBUG(KZONE_VMM, "[mm-map] " fmt "\n", ##__VA_ARGS__)

        alignas(PageSize) U64 bootstrap_address_space_l0[backend::TableEntries];
        alignas(PageSize) U64 bootstrap_address_space_l1[backend::TableEntries];
        alignas(PageSize) U64 bootstrap_address_space_l2[backend::TableEntries];
        alignas(PageSize) U64 kernel_address_space_l0[backend::TableEntries];
        alignas(PageSize) U64 kernel_address_space_l1[backend::TableEntries];
        alignas(PageSize) U64 kernel_address_space_l2[backend::TableEntries];
        MmState mm_state = {};
        bool bootstrapped;

        PhysAddr table_phys(const U64* table) {
            return reinterpret_cast<PhysAddr>(table) - KernelVaBase;
        }

        U64* table_virt(PhysAddr phys) {
            return reinterpret_cast<U64*>(phys + KernelVaBase);
        }

        U64 make_table_descriptor(const U64* table) {
            return table_phys(table) | backend::DescTable;
        }

        U64 make_block_descriptor(PhysAddr phys, U64 flags) {
            return phys | flags | backend::DescBlock;
        }

        U64 make_page_descriptor(PhysAddr phys, U64 flags) {
            return phys | flags | backend::DescPage;
        }

        void clear_page_tables(void) {
            memzero(bootstrap_address_space_l0, sizeof(bootstrap_address_space_l0));
            memzero(bootstrap_address_space_l1, sizeof(bootstrap_address_space_l1));
            memzero(bootstrap_address_space_l2, sizeof(bootstrap_address_space_l2));
            memzero(kernel_address_space_l0, sizeof(kernel_address_space_l0));
            memzero(kernel_address_space_l1, sizeof(kernel_address_space_l1));
            memzero(kernel_address_space_l2, sizeof(kernel_address_space_l2));
        }

        void map_low_gib(U64* l2_table) {
            for (U64 index = 0; index < backend::TableEntries; ++index) {
                const PhysAddr phys = static_cast<PhysAddr>(index << backend::L2Shift);
                const U64 flags = ((phys >= board_config::DeviceBase) && (phys < board_config::DeviceLimit))
                    ? backend::DeviceFlags
                    : backend::KernelCodeFlags;

                l2_table[index] = make_block_descriptor(phys, flags);
            }
        }

        void build_bootstrap_address_space(AddressSpace* address_space) {
            clear_page_tables();

            bootstrap_address_space_l0[0] = make_table_descriptor(bootstrap_address_space_l1);
            bootstrap_address_space_l1[0] = make_table_descriptor(bootstrap_address_space_l2);
            map_low_gib(bootstrap_address_space_l2);

            if constexpr (board_config::ExtraRamBase != 0ULL) {
                bootstrap_address_space_l1[1] = make_block_descriptor(board_config::ExtraRamBase, backend::KernelCodeFlags);
            }

            kernel_address_space_l0[0] = make_table_descriptor(kernel_address_space_l1);
            kernel_address_space_l1[0] = make_table_descriptor(kernel_address_space_l2);
            map_low_gib(kernel_address_space_l2);

            if constexpr (board_config::ExtraRamBase != 0ULL) {
                kernel_address_space_l1[1] = make_block_descriptor(board_config::ExtraRamBase, backend::KernelCodeFlags);
            }

            address_space->page_table_root = table_phys(bootstrap_address_space_l0);
            address_space->region_list = NULL;
            address_space->asid = 0;
            address_space->user_base = UserVaBase;
            address_space->user_limit = UserVaLimit;
            address_space->translation_table_l0 = 0U;
            address_space->translation_table_l1 = 0U;
            address_space->translation_table_l2 = 0U;

            mm_state.kernel_address_space.root_table = table_phys(kernel_address_space_l0);
            mm_state.kernel_address_space.kernel_base = KernelVaBase;
        }

        void initialize_user_address_space(AddressSpace* address_space, U64* l0_table, U64* l1_table, U64* l2_table) {
            memzero(l0_table, PageSize);
            memzero(l1_table, PageSize);
            memzero(l2_table, PageSize);

            l0_table[0] = make_table_descriptor(l1_table);
            l1_table[0] = make_table_descriptor(l2_table);
            // Keep the kernel's low-memory identity map visible to EL1 while switching from the
            // bootstrap thread into a user process. The current scheduler still performs the
            // TTBR0 handoff before it has moved onto the destination thread's private kernel stack,
            // so an entirely empty TTBR0 would make the live bootstrap stack vanish mid-switch.
            // User image and stack mappings overwrite the needed blocks with EL0 permissions later.
            map_low_gib(l2_table);

            address_space->page_table_root = table_phys(l0_table);
            address_space->region_list = NULL;
            address_space->asid = 0U;
            address_space->user_base = UserVaBase;
            address_space->user_limit = UserVaLimit;
            address_space->translation_table_l0 = reinterpret_cast<VirtAddr>(l0_table);
            address_space->translation_table_l1 = reinterpret_cast<VirtAddr>(l1_table);
            address_space->translation_table_l2 = reinterpret_cast<VirtAddr>(l2_table);
        }

        void write_mair(void) {
            const U64 mair = backend::MairValue;

            __asm__ volatile("msr mair_el1, %0\n"
                "isb\n"
                :
            : "r"(mair)
                : "memory");
        }

        void write_tcr(void) {
            const U64 tcr = backend::tcr_value();

            __asm__ volatile("msr tcr_el1, %0\n"
                "isb\n"
                :
            : "r"(tcr)
                : "memory");
        }

        void invalidate_tlb(void) {
            __asm__ volatile("tlbi vmalle1is\n"
                "dsb ish\n"
                "isb\n"
                ::: "memory");
        }

        U64 current_ttbr0(void) {
            U64 value;

            __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(value));
            return value;
        }

        U64 current_ttbr1(void) {
            U64 value;

            __asm__ volatile("mrs %0, ttbr1_el1" : "=r"(value));
            return value;
        }

        U64 current_sctlr(void) {
            U64 value;

            __asm__ volatile("mrs %0, sctlr_el1" : "=r"(value));
            return value;
        }

        U64 descriptor_flags(U64 mapping_flags) {
            if ((mapping_flags & PageDevice) != 0ULL) {
                return backend::DeviceFlags;
            }

            const bool executable = (mapping_flags & PageExecutable) != 0ULL;
            const bool writable = (mapping_flags & PageWritable) != 0ULL;
            const bool user = (mapping_flags & PageUser) != 0ULL;
            const U64 attr_index = ((mapping_flags & PageNoCache) != 0ULL)
                ? backend::AttrNonCacheable
                : backend::AttrNormal;

            U64 flags = (attr_index << 2) | backend::AccessFlag | backend::ShInner;

            if (user) {
                flags |= writable ? backend::ApUserRw : backend::ApUserRo;
            }
            else {
                flags |= writable ? backend::ApKernelRw : backend::ApKernelRo;
            }

            if (!executable) {
                flags |= backend::Pxn | backend::Uxn;
            }
            else if (user) {
                flags |= backend::Pxn;
            }
            else {
                flags |= backend::Uxn;
            }

            return flags;
        }

        U64* resolve_level2_entry(const AddressSpace* address_space, VirtAddr address) {
            const PhysAddr root_phys = MemoryManager::is_kernel_address(address)
                ? mm_state.kernel_address_space.root_table
                : address_space->page_table_root;
            U64* l0;
            U64 l0_entry;
            U64* l1;
            U64 l1_entry;

            if (root_phys == 0U) {
                return NULL;
            }

            l0 = table_virt(root_phys);
            l0_entry = l0[backend::level_index(address, backend::L0Shift)];
            if ((l0_entry & backend::DescTable) != backend::DescTable) {
                return NULL;
            }

            l1 = table_virt(l0_entry & backend::OutputAddressMask);
            l1_entry = l1[backend::level_index(address, backend::L1Shift)];
            if ((l1_entry & backend::DescTable) != backend::DescTable) {
                return NULL;
            }

            return &table_virt(l1_entry & backend::OutputAddressMask)[backend::level_index(address, backend::L2Shift)];
        }

        /*
         * Resolve one level-3 page-table entry for a page-sized user mapping.
         *
         * Page-backed surfaces keep the existing slot-based virtual layout, but
         * only populate the pages that the backing store actually needs. When a
         * slot transitions away from the bootstrap L2 identity block, this
         * helper installs a private L3 table for that one 2 MiB range.
         *
         * @param address_space Address space that owns the mapping.
         * @param address Virtual address being mapped or unmapped.
         * @param create_missing True when the helper may allocate the L3 table.
         * @param status_out Receives the resolution status.
         * @return Pointer to the target L3 entry, or NULL on failure.
         */
        U64* resolve_level3_entry(AddressSpace* address_space, VirtAddr address, bool create_missing, Status* status_out) {
            U64* l2_entry;
            U64 descriptor;
            U64* l3_table;

            if (status_out == NULL) {
                return NULL;
            }

            *status_out = StatusInvalidArgument;
            if (address_space == NULL) {
                return NULL;
            }

            l2_entry = resolve_level2_entry(address_space, address);
            if (l2_entry == NULL) {
                *status_out = StatusNotSupported;
                return NULL;
            }

            descriptor = *l2_entry;
            if ((descriptor & backend::DescTable) == backend::DescTable) {
                l3_table = table_virt(descriptor & backend::OutputAddressMask);
                *status_out = StatusOK;
                return &l3_table[backend::level_index(address, backend::PageShift)];
            }

            if (!create_missing) {
                *status_out = StatusNotSupported;
                return NULL;
            }

            l3_table = static_cast<U64*>(Heap::alloc(PageSize, PageSize));
            if (l3_table == NULL) {
                *status_out = StatusNoMemory;
                return NULL;
            }

            memzero(l3_table, PageSize);
            *l2_entry = make_table_descriptor(l3_table);
            *status_out = StatusOK;
            return &l3_table[backend::level_index(address, backend::PageShift)];
        }

        /*
         * Return whether one L3 page table has any live mappings left.
         *
         * @param table Level-3 table to inspect.
         * @return True when every entry is invalid.
         */
        bool level3_table_empty(const U64* table) {
            if (table == NULL) {
                return true;
            }

            for (Size index = 0U; index < backend::TableEntries; ++index) {
                if (table[index] != backend::DescInvalid) {
                    return false;
                }
            }

            return true;
        }

        /*
         * Free one now-empty L3 table and invalidate its parent L2 entry.
         *
         * @param address_space Address space that owns the table.
         * @param address One address inside the 2 MiB slot to inspect.
         * @return Nothing.
         */
        void release_empty_level3_table(AddressSpace* address_space, VirtAddr address) {
            U64* l2_entry;
            U64 descriptor;
            U64* l3_table;

            if (address_space == NULL) {
                return;
            }

            l2_entry = resolve_level2_entry(address_space, address);
            if (l2_entry == NULL) {
                return;
            }

            descriptor = *l2_entry;
            if ((descriptor & backend::DescTable) != backend::DescTable) {
                return;
            }

            l3_table = table_virt(descriptor & backend::OutputAddressMask);
            if (!level3_table_empty(l3_table)) {
                return;
            }

            Heap::free(l3_table);
            *l2_entry = backend::DescInvalid;
        }

    } // namespace

    Status MemoryManager::bootstrap(void) {
        if (bootstrapped) {
            return StatusOK;
        }

        build_bootstrap_address_space(&mm_state.bootstrap_address_space);
        write_mair();
        write_tcr();

        if (switch_to(&mm_state.bootstrap_address_space) != StatusOK) {
            return StatusFault;
        }

        bootstrapped = true;
        return StatusOK;
    }

    Status MemoryManager::map(AddressSpace* address_space, const VmMapping* mapping) {
        const bool block_aligned = ((mapping->virtual_base | mapping->physical_base | mapping->length) & (backend::L2BlockSize - 1U)) == 0U;
        const bool page_aligned = ((mapping->virtual_base | mapping->physical_base | mapping->length) & (PageSize - 1U)) == 0U;

        if ((address_space == NULL) || (mapping == NULL) || (mapping->length == 0U)) {
            return StatusInvalidArgument;
        }
        if (!page_aligned) {
            MMU_MAP_TRACE(
                "reject page-alignment root=%#llx vaddr=%#llx paddr=%#llx len=%#llx align_mask=%#llx",
                static_cast<unsigned long long>(address_space->page_table_root),
                static_cast<unsigned long long>(mapping->virtual_base),
                static_cast<unsigned long long>(mapping->physical_base),
                static_cast<unsigned long long>(mapping->length),
                static_cast<unsigned long long>((mapping->virtual_base | mapping->physical_base | mapping->length) & (PageSize - 1U)));
            return StatusNotSupported;
        }

        const U64 flags = descriptor_flags(mapping->flags);

        MMU_MAP_TRACE(
            "map root=%#llx vaddr=%#llx paddr=%#llx len=%#llx flags=%#llx",
            static_cast<unsigned long long>(address_space->page_table_root),
            static_cast<unsigned long long>(mapping->virtual_base),
            static_cast<unsigned long long>(mapping->physical_base),
            static_cast<unsigned long long>(mapping->length),
            static_cast<unsigned long long>(flags));

        if (block_aligned) {
            for (Size offset = 0; offset < mapping->length; offset += backend::L2BlockSize) {
                U64* entry = resolve_level2_entry(address_space, mapping->virtual_base + offset);

                if (entry == NULL) {
                    MMU_MAP_TRACE(
                        "reject missing-l2 root=%#llx vaddr=%#llx offset=%#llx",
                        static_cast<unsigned long long>(address_space->page_table_root),
                        static_cast<unsigned long long>(mapping->virtual_base),
                        static_cast<unsigned long long>(offset));
                    return StatusNotSupported;
                }

                *entry = make_block_descriptor(mapping->physical_base + offset, flags);
            }
        }
        else {
            for (Size offset = 0; offset < mapping->length; offset += PageSize) {
                Status status;
                U64* entry = resolve_level3_entry(address_space, mapping->virtual_base + offset, true, &status);

                if (entry == NULL) {
                    MMU_MAP_TRACE(
                        "reject missing-l3 root=%#llx vaddr=%#llx offset=%#llx status=%d",
                        static_cast<unsigned long long>(address_space->page_table_root),
                        static_cast<unsigned long long>(mapping->virtual_base),
                        static_cast<unsigned long long>(offset),
                        static_cast<int>(status));
                    return status;
                }

                *entry = make_page_descriptor(mapping->physical_base + offset, flags);
            }
        }

        invalidate_tlb();
        return StatusOK;
    }

    Status MemoryManager::unmap(AddressSpace* address_space, VirtAddr address, Size length) {
        const bool block_aligned = ((address | length) & (backend::L2BlockSize - 1U)) == 0U;

        if ((address_space == NULL) || (length == 0U)) {
            return StatusInvalidArgument;
        }
        if (((address | length) & (PageSize - 1U)) != 0U) {
            return StatusNotSupported;
        }

        if (block_aligned) {
            for (Size offset = 0; offset < length; offset += backend::L2BlockSize) {
                U64* entry = resolve_level2_entry(address_space, address + offset);

                if (entry == NULL) {
                    return StatusNotSupported;
                }

                *entry = backend::DescInvalid;
            }
        }
        else {
            for (Size offset = 0; offset < length; offset += PageSize) {
                Status status;
                U64* entry = resolve_level3_entry(address_space, address + offset, false, &status);

                if (entry == NULL) {
                    return status;
                }

                *entry = backend::DescInvalid;
            }

            for (VirtAddr block_base = address & ~(backend::L2BlockSize - 1U);
                block_base < (address + length);
                block_base += backend::L2BlockSize) {
                release_empty_level3_table(address_space, block_base);
            }
        }

        invalidate_tlb();
        return StatusOK;
    }

    Status MemoryManager::switch_to(const AddressSpace* address_space) {
        if ((address_space == NULL) || (address_space->page_table_root == 0U) || (mm_state.kernel_address_space.root_table == 0U)) {
            return StatusInvalidArgument;
        }

        __asm__ volatile("msr ttbr0_el1, %0\n"
            "msr ttbr1_el1, %1\n"
            "dsb ish\n"
            "isb\n"
            :
        : "r"(address_space->page_table_root), "r"(mm_state.kernel_address_space.root_table)
            : "memory");

        invalidate_tlb();
        mm_state.current_address_space = *address_space;
        return StatusOK;
    }

    Status MemoryManager::init(void) {
        return bootstrap();
    }

    Status MemoryManager::create_user_address_space(AddressSpace* address_space) {
        U64* l0_table;
        U64* l1_table;
        U64* l2_table;

        if (address_space == NULL) {
            return StatusInvalidArgument;
        }
        if (!bootstrapped) {
            Status status = bootstrap();

            if (status != StatusOK) {
                return status;
            }
        }

        l0_table = static_cast<U64*>(Heap::alloc(PageSize, PageSize));
        l1_table = static_cast<U64*>(Heap::alloc(PageSize, PageSize));
        l2_table = static_cast<U64*>(Heap::alloc(PageSize, PageSize));
        if ((l0_table == NULL) || (l1_table == NULL) || (l2_table == NULL)) {
            if (l0_table != NULL) {
                Heap::free(l0_table);
            }
            if (l1_table != NULL) {
                Heap::free(l1_table);
            }
            if (l2_table != NULL) {
                Heap::free(l2_table);
            }
            return StatusNoMemory;
        }

        initialize_user_address_space(address_space, l0_table, l1_table, l2_table);
        return StatusOK;
    }

    Status MemoryManager::destroy_user_address_space(AddressSpace* address_space) {
        U64* l2_table;

        if (address_space == NULL) {
            return StatusInvalidArgument;
        }
        if (address_space->page_table_root == 0U) {
            memzero(address_space, sizeof(*address_space));
            return StatusOK;
        }

        l2_table = reinterpret_cast<U64*>(address_space->translation_table_l2);
        if (l2_table != NULL) {
            for (Size index = 0U; index < backend::TableEntries; ++index) {
                const U64 descriptor = l2_table[index];
                const PhysAddr table_phys = descriptor & backend::OutputAddressMask;

                if ((descriptor & backend::DescTable) != backend::DescTable) {
                    continue;
                }
                // User page-table teardown only owns heap-backed L3 tables allocated
                // on demand for page mappings. A zero output address can never name a
                // live heap table here, so skip it instead of translating physical 0
                // into the kernel higher-half alias and handing that bogus pointer to
                // the heap during cleanup.
                if (table_phys == 0U) {
                    continue;
                }

                Heap::free(table_virt(table_phys));
            }
        }

        if (address_space->translation_table_l2 != 0U) {
            Heap::free(reinterpret_cast<void*>(address_space->translation_table_l2));
        }
        if (address_space->translation_table_l1 != 0U) {
            Heap::free(reinterpret_cast<void*>(address_space->translation_table_l1));
        }
        if (address_space->translation_table_l0 != 0U) {
            Heap::free(reinterpret_cast<void*>(address_space->translation_table_l0));
        }
        memzero(address_space, sizeof(*address_space));
        return StatusOK;
    }

    const AddressSpace* MemoryManager::bootstrap_address_space(void) {
        return &mm_state.bootstrap_address_space;
    }

    const AddressSpace* MemoryManager::current_address_space(void) {
        return &mm_state.current_address_space;
    }

    VirtAddr MemoryManager::physical_to_kernel(PhysAddr address) {
        return address + KernelVaBase;
    }

    PhysAddr MemoryManager::kernel_to_physical(VirtAddr address) {
        return is_kernel_address(address) ? (address - KernelVaBase) : address;
    }

    Status MemoryManager::self_test(VmSelfTestResult* result) {
        const U64 process_address_space_root = current_ttbr0() & backend::OutputAddressMask;
        const U64 kernel_address_space_root = current_ttbr1() & backend::OutputAddressMask;

        if (result == NULL) {
            return StatusInvalidArgument;
        }

        result->mmu_enabled = (current_sctlr() & 1U) != 0U;
        result->process_address_space_root = process_address_space_root;
        result->kernel_address_space_root = kernel_address_space_root;
        result->kernel_text_address = reinterpret_cast<VirtAddr>(&kernel_entry);
        result->kernel_text_physical = kernel_to_physical(result->kernel_text_address);
        result->kernel_text_is_high = is_kernel_address(result->kernel_text_address);
        result->current_address_space_active =
            (process_address_space_root == mm_state.current_address_space.page_table_root) &&
            (kernel_address_space_root == mm_state.kernel_address_space.root_table);
        result->translation_roundtrip =
            (physical_to_kernel(result->kernel_text_physical) == result->kernel_text_address);
        return StatusOK;
    }

    bool MemoryManager::is_user_address(VirtAddr address) {
        return address <= UserVaLimit;
    }

    bool MemoryManager::is_kernel_address(VirtAddr address) {
        return address >= KernelVaBase;
    }

} // namespace mm