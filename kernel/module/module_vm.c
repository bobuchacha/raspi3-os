#include "arch/cortex-a53/mmu.h"
#include "module.h"
#include "memory.h"
#include "log.h"
#include "utils.h"

static Address module_region_next = MODULE_REGION_BASE;

/**
 * Extract the physical address stored in a translation table entry.
 *
 * Args:
 *   entry: Raw MMU table entry.
 *
 * Behavior:
 *   Masks off descriptor bits and high attribute fields to recover the backing
 *   physical page address.
 *
 * Returns:
 *   Physical page base encoded by the entry.
 */
static Address module_vm_entry_phys(TableEntry entry) {
    return (Address)(entry & ~((((pentry_t)1 << 12) - 1) | ((((pentry_t)1 << 6) - 1) << 53)));
}

/**
 * Convert a module-region virtual address into a TTBR1-relative offset.
 *
 * Args:
 *   va: Virtual address within the module window.
 *
 * Behavior:
 *   Strips the kernel virtual base so lower-level page-table indexing can be
 *   done with region-relative offsets.
 *
 * Returns:
 *   Offset from `VA_START`.
 */
static inline ULong module_vm_offset(Address va) {
    return (ULong)(va - VA_START);
}

/**
 * Check whether a virtual address belongs to the module VM window.
 *
 * Args:
 *   va: Virtual address to validate.
 *
 * Behavior:
 *   Confirms the address falls inside the reserved module region and is page
 *   aligned.
 *
 * Returns:
 *   `true` for valid module virtual addresses, otherwise `false`.
 */
static inline Bool module_vm_is_valid_va(Address va) {
    return va >= MODULE_REGION_BASE && va < MODULE_REGION_LIMIT && mem_is_page_aligned((ULong)va);
}

/**
 * Read the cache-line size reported by CTR_EL0.
 *
 * Args:
 *   shift: Bit position of the cache-line field to extract.
 *
 * Behavior:
 *   Uses the ARM CTR_EL0 format where cache line size is encoded in words and
 *   converts it into bytes.
 *
 * Returns:
 *   Cache line size in bytes.
 */
static ULong module_vm_cache_line_bytes(unsigned int shift) {
    ULong ctr_el0;
    ULong line_words;

    asm volatile("mrs %0, ctr_el0" : "=r"(ctr_el0));
    line_words = (ctr_el0 >> shift) & 0xFUL;
    return 4UL << line_words;
}

/**
 * Flush module-related TLB state after page-table edits.
 *
 * Args:
 *   None.
 *
 * Behavior:
 *   Performs the ARM barrier sequence required after updating EL1 translation
 *   tables.
 *
 * Returns:
 *   Nothing.
 */
static void module_vm_invalidate_tlb(void) {
    asm volatile(
        "dsb ishst\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb\n"
        ::: "memory");
}

/**
 * Read the kernel TTBR1 root page-table address.
 *
 * Args:
 *   None.
 *
 * Behavior:
 *   Reads TTBR1_EL1 and masks off attribute bits to recover the page-aligned
 *   root table base.
 *
 * Returns:
 *   Physical address of the TTBR1 root table.
 */
static Address module_vm_ttbr1_root(void) {
    Address root;

    asm volatile("mrs %0, ttbr1_el1" : "=r"(root));
    return root & MM_PAGE_MASK;
}

/**
 * Walk or create page tables until the final PTE for a module VA is available.
 *
 * Args:
 *   va: Page-aligned module virtual address.
 *   pte_out: Receives the pointer to the PTE page.
 *   pte_index_out: Receives the slot index inside that PTE page.
 *
 * Behavior:
 *   Traverses TTBR1 page tables, allocating missing intermediate tables on
 *   demand so callers can map, unmap, or rewrite a leaf entry.
 *
 * Returns:
 *   `0` on success, or `-1` if the address is invalid or a table allocation
 *   fails.
 */
static int module_vm_ensure_pte(Address va, TableEntry** pte_out, ULong* pte_index_out) {
    ULong offset;
    ULong pgd_index;
    ULong pud_index;
    ULong pmd_index;
    ULong pte_index;
    Address pgd_pa;
    Address pud_pa;
    TableEntry pud_entry;
    TableEntry pmd_entry;
    Address pmd_pa;
    Address pte_pa;
    TableEntry* pgd;
    TableEntry* pud;
    TableEntry* pmd;

    if (!module_vm_is_valid_va(va) || !pte_out || !pte_index_out) {
        return -1;
    }

    // Decode the address into per-level indexes for the kernel TTBR1 tables.
    offset = module_vm_offset(va);
    pgd_index = (va >> MM_PGD_SHIFT) & (MM_PTRS_PER_TABLE - 1);
    pud_index = (offset >> MM_PUD_SHIFT) & (MM_PTRS_PER_TABLE - 1);
    pmd_index = (offset >> MM_PMD_SHIFT) & (MM_PTRS_PER_TABLE - 1);
    pte_index = (offset >> MM_PAGE_SHIFT) & (MM_PTRS_PER_TABLE - 1);

    pgd_pa = module_vm_ttbr1_root();
    if (!pgd_pa) {
        return -1;
    }
    pgd = (TableEntry*)mem_phys_to_virt((PhysAddr)pgd_pa);
    if ((pgd[pgd_index] & 0x3U) != PT_TABLE_ENTRY) {
        // Allocate a new PUD table when the root slot is still empty.
        pud_pa = mem_alloc_page();
        if (!pud_pa) {
            return -1;
        }
        memzero(mem_phys_to_virt((PhysAddr)pud_pa), PAGE_SIZE);
        pgd[pgd_index] = (TableEntry)pud_pa | PT_TABLE_ENTRY;
    }

    pud_pa = module_vm_entry_phys(pgd[pgd_index]);
    pud = (TableEntry*)mem_phys_to_virt((PhysAddr)pud_pa);
    pud_entry = pud[pud_index];
    if ((pud_entry & 0x3U) != PT_TABLE_ENTRY) {
        // Materialize the PMD table lazily for this address range.
        pmd_pa = mem_alloc_page();
        if (!pmd_pa) {
            return -1;
        }
        memzero(mem_phys_to_virt((PhysAddr)pmd_pa), PAGE_SIZE);
        pud[pud_index] = (TableEntry)pmd_pa | PT_TABLE_ENTRY;
        pud_entry = pud[pud_index];
    }

    pmd_pa = module_vm_entry_phys(pud_entry);
    pmd = (TableEntry*)mem_phys_to_virt((PhysAddr)pmd_pa);
    pmd_entry = pmd[pmd_index];
    if ((pmd_entry & 0x3U) != PT_TABLE_ENTRY) {
        // Create the final PTE table only when a module page is first touched.
        pte_pa = mem_alloc_page();
        if (!pte_pa) {
            return -1;
        }
        memzero(mem_phys_to_virt((PhysAddr)pte_pa), PAGE_SIZE);
        pmd[pmd_index] = (TableEntry)pte_pa | PT_TABLE_ENTRY;
        pmd_entry = pmd[pmd_index];
    }

    pte_pa = module_vm_entry_phys(pmd_entry);
    *pte_out = (TableEntry*)mem_phys_to_virt((PhysAddr)pte_pa);
    *pte_index_out = pte_index;
    return 0;
}

/**
 * Return the base of the reserved module virtual-address range.
 *
 * Args:
 *   None.
 *
 * Behavior:
 *   Exposes the constant base so diagnostic code can print or reuse it.
 *
 * Returns:
 *   Virtual base address of the module region.
 */
unsigned long module_vm_base(void) {
    return MODULE_REGION_BASE;
}

/**
 * Return the upper limit of the reserved module virtual-address range.
 *
 * Args:
 *   None.
 *
 * Behavior:
 *   Exposes the end of the module reservation window.
 *
 * Returns:
 *   Exclusive virtual limit of the module region.
 */
unsigned long module_vm_limit(void) {
    return MODULE_REGION_LIMIT;
}

/**
 * Log the module virtual-memory reservation range.
 *
 * Args:
 *   None.
 *
 * Behavior:
 *   Emits a diagnostic line that can be used during startup verification.
 *
 * Returns:
 *   Nothing.
 */
void module_vm_note(void) {
    log_info("Module VM region reserved at [0x%lX, 0x%lX)", module_vm_base(), module_vm_limit());
}

/**
 * Reserve a contiguous chunk of the module virtual-address window.
 *
 * Args:
 *   size: Number of bytes the caller needs.
 *   align: Required alignment for the allocation.
 *
 * Behavior:
 *   Bumps the module-region frontier upward after aligning both the base and
 *   the size to page-friendly boundaries.
 *
 * Returns:
 *   Base virtual address of the reservation, or `0` when the region is full or
 *   the request is invalid.
 */
Address module_vm_reserve(ULong size, ULong align) {
    Address base;
    Address end;

    if (size == 0) {
        return 0;
    }
    if (align < PAGE_SIZE) {
        align = PAGE_SIZE;
    }
    // Align the next reservation so section mappings can preserve requested gaps.
    base = (Address)((module_region_next + align - 1UL) & ~(align - 1UL));
    end = base + mem_align_up(size);
    if (end < base || end > MODULE_REGION_LIMIT) {
        return 0;
    }

    module_region_next = end;
    return base;
}

/**
 * Map one physical page into the module virtual-address window.
 *
 * Args:
 *   va: Destination virtual address.
 *   pa: Backing physical page.
 *   flags: PTE flags for the mapping.
 *
 * Behavior:
 *   Ensures the required page tables exist, rejects duplicate mappings, writes
 *   the leaf entry, and invalidates the TLB.
 *
 * Returns:
 *   `0` on success, or `-1` on validation, allocation, or remap failure.
 */
int module_vm_map_page(Address va, Address pa, Flags flags) {
    TableEntry* pte;
    ULong pte_index;

    if (!module_vm_is_valid_va(va) || !mem_is_valid_phys_page((PhysAddr)pa)) {
        return -1;
    }
    if (module_vm_ensure_pte(va, &pte, &pte_index) != 0) {
        return -1;
    }
    if (pte[pte_index] != 0) {
        // Refuse to stomp an existing mapping because that would leak pages.
        log_warning("Module VM map refused: VA 0x%lX already mapped", va);
        return -1;
    }

    pte[pte_index] = (TableEntry)(pa & MM_PAGE_MASK) | flags | PT_TABLE_ENTRY;
    module_vm_invalidate_tlb();
    return 0;
}

/**
 * Remove one page mapping from the module address range.
 *
 * Args:
 *   va: Module virtual address to unmap.
 *
 * Behavior:
 *   Locates the leaf PTE, clears it, and invalidates the TLB entry.
 *
 * Returns:
 *   `0` on success, or `-1` when the address is invalid or not mapped.
 */
int module_vm_unmap_page(Address va) {
    TableEntry* pte;
    ULong pte_index;

    if (!module_vm_is_valid_va(va)) {
        return -1;
    }
    if (module_vm_ensure_pte(va, &pte, &pte_index) != 0 || pte[pte_index] == 0) {
        return -1;
    }

    pte[pte_index] = 0;
    module_vm_invalidate_tlb();
    return 0;
}

/**
 * Rewrite page permissions for an existing module mapping.
 *
 * Args:
 *   va: Module virtual address whose flags should change.
 *   flags: Replacement PTE flags.
 *
 * Behavior:
 *   Preserves the mapped physical address while updating its access flags and
 *   then flushes the TLB.
 *
 * Returns:
 *   `0` on success, or `-1` when the address is invalid or unmapped.
 */
int module_vm_update_page_flags(Address va, Flags flags) {
    TableEntry* pte;
    ULong pte_index;
    TableEntry entry;

    if (!module_vm_is_valid_va(va)) {
        return -1;
    }
    if (module_vm_ensure_pte(va, &pte, &pte_index) != 0 || pte[pte_index] == 0) {
        return -1;
    }

    entry = module_vm_entry_phys(pte[pte_index]);
    pte[pte_index] = entry | flags | PT_TABLE_ENTRY;
    module_vm_invalidate_tlb();
    return 0;
}

/**
 * Synchronize instruction-cache visibility after writing module code pages.
 *
 * Args:
 *   start_va: First virtual address written by the loader.
 *   size: Number of bytes that may contain executable code.
 *
 * Behavior:
 *   Cleans the data cache to the point of unification, invalidates matching
 *   instruction-cache lines, and executes the required barrier sequence.
 *
 * Returns:
 *   Nothing.
 */
void module_vm_sync_icache(Address start_va, ULong size) {
    Address end_va;
    ULong dcache_line;
    ULong icache_line;

    if (size == 0) {
        return;
    }

    end_va = start_va + size;
    if (end_va < start_va) {
        return;
    }

    dcache_line = module_vm_cache_line_bytes(16);
    icache_line = module_vm_cache_line_bytes(0);
    if (dcache_line == 0) {
        dcache_line = 64;
    }
    if (icache_line == 0) {
        icache_line = 64;
    }

    // Push newly written bytes out of the data cache before invalidating I-cache lines.
    for (Address va = start_va & ~(dcache_line - 1UL); va < end_va; va += dcache_line) {
        asm volatile("dc cvau, %0" :: "r"(va) : "memory");
    }
    asm volatile("dsb ish" ::: "memory");

    // Invalidate every instruction-cache line that overlaps the written range.
    for (Address va = start_va & ~(icache_line - 1UL); va < end_va; va += icache_line) {
        asm volatile("ic ivau, %0" :: "r"(va) : "memory");
    }
    asm volatile(
        "dsb ish\n"
        "isb\n"
        ::: "memory");
}