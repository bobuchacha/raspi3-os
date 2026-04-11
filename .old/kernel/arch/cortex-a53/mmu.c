#define LOG_ENABLE_TRACE 0

#include "ros.h"
#include "printf.h"
#include "log.h"
#include "arch/cortex-a53/mmu.h"
#include "task.h"
#include "memory.h"
#include "device.h"
#include "utils.h"

#define MM_TYPE_PAGE_TABLE 0x3
#define MM_TYPE_PAGE 0x3
#define MM_TYPE_BLOCK 0x1
#define MM_ACCESS (0x1 << 10)
#define MM_ACCESS_PERMISSION (0x01 << 6)

/*
 * Memory region attributes:
 *
 *   n = AttrIndx[2:0]
 *			n	MAIR
 *   DEVICE_nGnRnE	000	00000000
 *   NORMAL_NC		001	01000100
 */
#define MT_DEVICE_nGnRnE 0x0
#define MT_NORMAL_NC 0x1
#define MT_DEVICE_nGnRnE_FLAGS 0x00
#define MT_NORMAL_NC_FLAGS 0x44
#define MAIR_VALUE (MT_DEVICE_nGnRnE_FLAGS << (8 * MT_DEVICE_nGnRnE)) | (MT_NORMAL_NC_FLAGS << (8 * MT_NORMAL_NC))

#define MMU_FLAGS (MM_TYPE_BLOCK | (MT_NORMAL_NC << 2) | MM_ACCESS)
#define MMU_DEVICE_FLAGS (MM_TYPE_BLOCK | (MT_DEVICE_nGnRnE << 2) | MM_ACCESS)
#define MMU_PTE_FLAGS (MM_TYPE_PAGE | (MT_NORMAL_NC << 2) | MM_ACCESS | MM_ACCESS_PERMISSION)

static int process_find_user_page_index(Task* task, Address va) {
	Address page_va = va & MM_PAGE_MASK;

	if (!task) {
		return -1;
	}

	for (int index = 0; index < task->mm.user_pages_count; index++) {
		if (task->mm.user_pages[index].virt_addr == page_va) {
			return index;
		}
	}

	return -1;
}

static Bool process_validate_mapping_inputs(Task* task, Address pa, Address va) {
	if (!task) {
		log_error("MMU map failed: null task");
		return false;
	}
	if (!mem_is_valid_phys_page((PhysAddr)pa)) {
		log_error("MMU map failed: invalid physical page 0x%lX", pa);
		return false;
	}
	if (!mem_is_page_aligned((ULong)va)) {
		log_error("MMU map failed: unaligned virtual address 0x%lX", va);
		return false;
	}
	if (va == 0 || va >= USER_SHARED_INPUT_VIEW_LIMIT) {
		log_error("MMU map failed: user virtual address 0x%lX out of range", va);
		return false;
	}
	return true;
}

static int process_track_user_page(Task* task, Address pa, Address va, UInt tracking_flags) {
	UserPage p = { pa, va, tracking_flags };

	if (!task || task->mm.user_pages_count >= MAX_PROCESS_PAGES) {
		return -1;
	}

	task->mm.user_pages[task->mm.user_pages_count++] = p;
	return 0;
}

static int process_track_kernel_table_page(Task* task, Address page) {
	if (!task || task->mm.kernel_pages_count >= MAX_PROCESS_PAGES) {
		return -1;
	}

	for (int index = 0; index < task->mm.kernel_pages_count; index++) {
		if ((task->mm.kernel_pages[index] & MM_PAGE_MASK) == (page & MM_PAGE_MASK)) {
			return 0;
		}
	}

	task->mm.kernel_pages[task->mm.kernel_pages_count++] = page;
	return 0;
}

static Address process_pte_phys_addr(TableEntry entry) {
	TableEntry masked = entry & ~((((pentry_t)1 << 12) - 1) | ((((pentry_t)1 << 6) - 1) << 53));

	return (Address)masked;
}

static int process_resolve_user_ptr(Task* task, Address va, UByte** ptr, ULong* bytes_left_in_page) {
	MmuWalkResult walk;
	ULong offset;

	if (!ptr || !bytes_left_in_page) {
		return -1;
	}
	if (process_walk_page(task, va & MM_PAGE_MASK, &walk) != 0) {
		return -1;
	}

	offset = va & (PAGE_SIZE - 1);
	*ptr = (UByte*)mem_phys_to_virt(walk.phys_addr) + offset;
	*bytes_left_in_page = PAGE_SIZE - offset;
	return 0;
}

void _map_table_entry(TableEntry* pte, Address va, Address pa, Flags flags) {
	unsigned long index = va >> MM_PAGE_SHIFT;
	index = index & (MM_PTRS_PER_TABLE - 1);
	// _tracef(">>>>>>>>>> Mapping PTE: VA: 0x%lX Index: %d\n", va, index);
	// unsigned long entry = pa | flags | PT_BLOCK_ENTRY;      // this is a block
	unsigned long entry = pa | flags | 3; // this is a block
	pte[index] = entry;
}

Address _map_table(TableEntry* table, ULong shift, Address va, Bool* is_table_new) {
	// _tracef("mapping table 0x%x", table);
	unsigned long index = va >> shift;
	// _tracef(">>>>>>>>>> Mapping Directory: VA: 0x%lX >> %d Index: %d\n", va,shift, index);
	index = index & (MM_PTRS_PER_TABLE - 1);
	if (!table[index]) {
		*is_table_new = true;
		Address next_level_table = (Address)mem_alloc_page();
		TableEntry entry = (ULong)next_level_table | PT_TABLE_ENTRY;
		table[index] = entry;
		return next_level_table;
	}
	else {
		*is_table_new = 0;
	}
	return table[index] & MM_PAGE_MASK;
}

/**
 * Record one user mapping in the task bookkeeping array.
 *
 * Args:
 *   task: Task that owns the mapping metadata.
 *   pa: Backing physical page address.
 *   va: User virtual page address.
 *   tracking_flags: Ownership metadata used during cleanup.
 *
 * Returns:
 *   Nothing. The caller must ensure capacity has already been checked.
 */
 /**
  * Map one physical page into a task user address space and track its ownership.
  *
  * Args:
  *   task: Task receiving the mapping.
  *   pa: Physical page base.
  *   va: User virtual page address.
  *   flags: Architecture-specific page attributes.
  *   tracking_flags: Ownership metadata for later cleanup.
  *
  * Returns:
  *   `0` on success, or `-1` when validation, duplicate detection, or table allocation fails.
  */
static int process_map_page_internal(Task* task, Address pa, Address va, Flags flags, UInt tracking_flags, Bool retain_phys) {
	Address pgd;
	Address page_pa = pa & MM_PAGE_MASK;
	Address page_va = va & MM_PAGE_MASK;

	if (!process_validate_mapping_inputs(task, page_pa, page_va)) {
		return -1;
	}
	if (task->mm.user_pages_count >= MAX_PROCESS_PAGES) {
		kerror("Too many user pages for task %d", task->id);
		return -1;
	}
	if (process_find_user_page_index(task, page_va) >= 0) {
		log_warning("MMU map refused: VA 0x%lX is already mapped in task %d", page_va, task->id);
		return -1;
	}
	if (task->mm.kernel_pages_count + (task->mm.pgd ? 3 : 4) > MAX_PROCESS_PAGES) {
		log_error("MMU map failed: task %d has no room for page-table bookkeeping", task->id);
		return -1;
	}

	if (retain_phys) {
		mem_retain_page(page_pa);
	}

	// create PGD if task doesnt have it yet
	if (!task->mm.pgd) {
		task->mm.pgd = mem_alloc_page();
		if (!task->mm.pgd || process_track_kernel_table_page(task, task->mm.pgd) < 0) {
			if (retain_phys) {
				mem_free_page(page_pa);
			}
			return -1;
		}
	}
	pgd = task->mm.pgd;

	Bool is_table_new;

	Address pud = _map_table((TableEntry*)(pgd + VA_START), MM_PGD_SHIFT, page_va, &is_table_new);
	if (is_table_new) {
		if (process_track_kernel_table_page(task, pud) < 0) {
			if (retain_phys) {
				mem_free_page(page_pa);
			}
			return -1;
		}
	}

	Address pmd = _map_table((TableEntry*)(pud + VA_START), MM_PUD_SHIFT, page_va, &is_table_new);
	if (is_table_new) {
		if (process_track_kernel_table_page(task, pmd) < 0) {
			if (retain_phys) {
				mem_free_page(page_pa);
			}
			return -1;
		}
	}

	Address pte = _map_table((TableEntry*)(pmd + VA_START), MM_PMD_SHIFT, page_va, &is_table_new);
	if (is_table_new) {
		if (process_track_kernel_table_page(task, pte) < 0) {
			if (retain_phys) {
				mem_free_page(page_pa);
			}
			return -1;
		}
	}

	_map_table_entry((TableEntry*)(pte + VA_START), page_va, page_pa, flags);

	if (process_track_user_page(task, page_pa, page_va, tracking_flags) < 0) {
		_map_table_entry((TableEntry*)(pte + VA_START), page_va, 0, 0);
		mem_free_page(page_pa);
		return -1;
	}

	_trace("Mapped PA \x1b[32m0x%lX\x1b[0m => VA \x1b[33m0x%lX\x1b[0m (refs=%d)", page_pa, page_va, mem_get_page_refcount(page_pa));
	return 0;
}

/**
 * Map a task-owned physical page into a task user address space.
 *
 * Args:
 *   task: Task receiving the mapping.
 *   pa: Freshly allocated physical page base.
 *   va: User virtual page address.
 *   flags: Architecture-specific page attributes.
 *
 * Returns:
 *   Nothing. The page is tracked as task-owned for later cleanup.
 */
int process_map_page(Task* task, Address pa, Address va, Flags flags) {
	return process_map_page_internal(task, pa, va, flags, USER_PAGE_OWNED, false);
}

/**
 * Map a shared physical page into a task without transferring ownership.
 *
 * Args:
 *   task: Task receiving the mapping.
 *   pa: Shared physical page base already owned elsewhere.
 *   va: User virtual page address.
 *   flags: Architecture-specific page attributes.
 *
 * Returns:
 *   Nothing. The mapping is tracked as shared.
 */
int process_map_shared_page(Task* task, Address pa, Address va, Flags flags) {
	return process_map_page_internal(task, pa, va, flags, USER_PAGE_SHARED, true);
}

int process_walk_page(Task* task, Address va, MmuWalkResult* result) {
	Address page_va = va & MM_PAGE_MASK;
	Address pgd_addr;
	TableEntry* pgd;
	TableEntry* pud;
	TableEntry* pmd;
	TableEntry* pte;

	if (!result) {
		return -1;
	}

	pgd_addr = task ? task->mm.pgd : get_pgd();
	if (!pgd_addr) {
		return -1;
	}

	memzero((Address)result, sizeof(*result));
	result->virt_addr = page_va;
	result->pgd = pgd_addr;
	result->pgd_index = (page_va >> MM_PGD_SHIFT) & (MM_PTRS_PER_TABLE - 1);
	result->pud_index = (page_va >> MM_PUD_SHIFT) & (MM_PTRS_PER_TABLE - 1);
	result->pmd_index = (page_va >> MM_PMD_SHIFT) & (MM_PTRS_PER_TABLE - 1);
	result->pte_index = (page_va >> MM_PAGE_SHIFT) & (MM_PTRS_PER_TABLE - 1);

	pgd = (TableEntry*)(pgd_addr + VA_START);
	if (!pgd[result->pgd_index]) {
		return -1;
	}

	result->pud = pgd[result->pgd_index] & MM_PAGE_MASK;
	pud = (TableEntry*)(result->pud + VA_START);
	if (!pud[result->pud_index]) {
		return -1;
	}

	result->pmd = pud[result->pud_index] & MM_PAGE_MASK;
	pmd = (TableEntry*)(result->pmd + VA_START);
	if (!pmd[result->pmd_index]) {
		return -1;
	}

	result->pte = pmd[result->pmd_index] & MM_PAGE_MASK;
	pte = (TableEntry*)(result->pte + VA_START);
	if (!pte[result->pte_index]) {
		return -1;
	}

	result->entry = pte[result->pte_index];
	result->phys_addr = process_pte_phys_addr(result->entry);
	result->flags = result->entry & ~MM_PAGE_MASK;
	result->valid = true;
	return 0;
}

int process_copy_from_user(Task* task, Address va, void* buffer, ULong len) {
	Address current_va = va;
	UByte* dst = (UByte*)buffer;
	ULong remaining = len;

	if (!buffer && len != 0) {
		return -1;
	}

	while (remaining > 0) {
		UByte* src;
		ULong chunk;

		if (process_resolve_user_ptr(task, current_va, &src, &chunk) != 0) {
			return -1;
		}
		if (chunk > remaining) {
			chunk = remaining;
		}

		memcpy((Address)dst, (Address)src, (int)chunk);
		dst += chunk;
		current_va += chunk;
		remaining -= chunk;
	}

	return 0;
}

int process_copy_to_user(Task* task, Address va, const void* buffer, ULong len) {
	Address current_va = va;
	const UByte* src = (const UByte*)buffer;
	ULong remaining = len;

	if (!buffer && len != 0) {
		return -1;
	}

	while (remaining > 0) {
		UByte* dst;
		ULong chunk;

		if (process_resolve_user_ptr(task, current_va, &dst, &chunk) != 0) {
			return -1;
		}
		if (chunk > remaining) {
			chunk = remaining;
		}

		memcpy((Address)dst, (Address)src, (int)chunk);
		src += chunk;
		current_va += chunk;
		remaining -= chunk;
	}

	return 0;
}

int process_unmap_page(Task* task, Address va) {
	MmuWalkResult walk;
	int index;

	if (!task || !task->mm.pgd) {
		return -1;
	}

	Address page_va = va & MM_PAGE_MASK;
	if (!mem_is_page_aligned((ULong)page_va)) {
		return -1;
	}
	if (process_walk_page(task, page_va, &walk) != 0) {
		log_warning("MMU unmap failed: VA 0x%lX not mapped in task %d", page_va, task->id);
		return -1;
	}

	TableEntry* pte = (TableEntry*)(walk.pte + VA_START);
	pte[walk.pte_index] = 0;

	index = process_find_user_page_index(task, page_va);
	if (index >= 0) {
		mem_free_page(task->mm.user_pages[index].phys_addr);
		for (int shift = index; shift < task->mm.user_pages_count - 1; shift++) {
			task->mm.user_pages[shift] = task->mm.user_pages[shift + 1];
		}
		task->mm.user_pages_count--;
		task->mm.user_pages[task->mm.user_pages_count].phys_addr = 0;
		task->mm.user_pages[task->mm.user_pages_count].virt_addr = 0;
		task->mm.user_pages[task->mm.user_pages_count].flags = 0;
	}
	else {
		log_warning("MMU metadata stale: VA 0x%lX missing from task %d tracking", page_va, task->id);
	}

	_trace("Unmapped VA \x1b[33m0x%lX\x1b[0m from task %d", page_va, task->id);
	return 0;
}

int process_update_page_flags(Task* task, Address va, Flags flags) {
	MmuWalkResult walk;
	TableEntry* pte;

	if (process_walk_page(task, va, &walk) != 0) {
		return -1;
	}

	pte = (TableEntry*)(walk.pte + VA_START);
	pte[walk.pte_index] = (walk.phys_addr & MM_PAGE_MASK) | flags | 3;
	_trace("Updated flags for task %d VA \x1b[33m0x%lX\x1b[0m -> 0x%lX", task->id, walk.virt_addr, flags);
	return 0;
}

int process_remap_page(Task* task, Address pa, Address va, Flags flags, UInt tracking_flags) {
	MmuWalkResult walk;
	TableEntry* pte;
	int index;
	Address new_pa = pa & MM_PAGE_MASK;

	if (!process_validate_mapping_inputs(task, new_pa, va & MM_PAGE_MASK)) {
		return -1;
	}
	if (process_walk_page(task, va, &walk) != 0) {
		log_warning("MMU remap failed: VA 0x%lX not mapped in task %d", va & MM_PAGE_MASK, task ? task->id : -1);
		return -1;
	}

	index = process_find_user_page_index(task, va);
	if (index < 0) {
		log_warning("MMU remap failed: metadata missing for VA 0x%lX in task %d", va & MM_PAGE_MASK, task->id);
		return -1;
	}
	if (new_pa != walk.phys_addr && (tracking_flags & USER_PAGE_SHARED)) {
		mem_retain_page(new_pa);
	}

	pte = (TableEntry*)(walk.pte + VA_START);
	pte[walk.pte_index] = new_pa | flags | 3;

	if (new_pa != walk.phys_addr) {
		mem_free_page(walk.phys_addr);
	}

	task->mm.user_pages[index].phys_addr = new_pa;
	task->mm.user_pages[index].flags = tracking_flags;
	_trace("Remapped task %d VA \x1b[33m0x%lX\x1b[0m => PA \x1b[32m0x%lX\x1b[0m", task->id, walk.virt_addr, new_pa);
	return 0;
}

int process_dump_mappings(Task* task) {
	MmuWalkResult walk;

	if (!task) {
		return -1;
	}

	kprint("Task %d mappings (pgd=0x%lX)\r\n", task->id, task->mm.pgd);
	kprint("VA               PA               REFS  FLAGS        ATTR\r\n");
	kprint("----------------------------------------------------------------\r\n");
	for (int index = 0; index < task->mm.user_pages_count; index++) {
		UserPage* page = &task->mm.user_pages[index];
		const char* attr = (page->flags & USER_PAGE_SHARED) ? "shared" : "owned";
		if (process_walk_page(task, page->virt_addr, &walk) == 0) {
			kprint("0x%08lX       0x%08lX       %4d  0x%08lX  %s\r\n", walk.virt_addr, walk.phys_addr, mem_get_page_refcount(walk.phys_addr), walk.flags, attr);
		}
		else {
			kprint("0x%08lX       <stale>          ----  --------  %s\r\n", page->virt_addr, attr);
		}
	}
	return task->mm.user_pages_count;
}

int process_mmu_self_test(void) {
	Task task;
	MmuWalkResult walk;
	Address page_a;
	Address page_b;
	Address test_va = 0x12000;
	int result = -1;

	memzero((Address)&task, sizeof(task));
	task.id = -1;
	task.mm.heap_next = USER_HEAP_BASE;
	task.mm.dll_local_next = USER_SHARED_LIBRARY_LOCAL_BASE;

	page_a = mem_alloc_page();
	page_b = mem_alloc_page();
	if (!page_a || !page_b) {
		goto cleanup;
	}
	if (process_map_page(&task, page_a, test_va, PE_USER_DATA) != 0) {
		goto cleanup;
	}
	if (process_walk_page(&task, test_va, &walk) != 0 || walk.phys_addr != page_a) {
		goto cleanup;
	}
	if (process_update_page_flags(&task, test_va, PE_USER_RO) != 0) {
		goto cleanup;
	}
	if (process_walk_page(&task, test_va, &walk) != 0 || (walk.flags & PE_AP_USER_RO) != PE_AP_USER_RO) {
		goto cleanup;
	}
	if (process_remap_page(&task, page_b, test_va, PE_USER_DATA, USER_PAGE_OWNED) != 0) {
		goto cleanup;
	}
	if (mem_get_page_refcount(page_a) != 0) {
		goto cleanup;
	}
	if (process_unmap_page(&task, test_va) != 0 || mem_get_page_refcount(page_b) != 0) {
		goto cleanup;
	}
	result = 0;

cleanup:
	for (int index = 0; index < task.mm.user_pages_count; index++) {
		if (task.mm.user_pages[index].phys_addr) {
			mem_free_page(task.mm.user_pages[index].phys_addr);
		}
	}
	for (int index = 0; index < task.mm.kernel_pages_count; index++) {
		mem_free_page(task.mm.kernel_pages[index]);
	}
	if (result == 0) {
		log_info("MMU self-test passed");
	}
	else {
		log_error("MMU self-test failed");
	}
	return result;
}

unsigned long mem_handle_data_abort(ulong addr, ulong esr) {
	_trace("Called by process %d. Requested access to VA \x1b[33m0x%lX\x1b[0m", current_task->id, addr);
	unsigned long dfs = (esr & 0b111111);
	if ((dfs & 0b111100) == 0b100) {
		Address page = mem_alloc_page();
		if (!page) {
			_trace("Page not allocated");
			return -1;
		}
		if (process_map_page(current_task, page, (Address)addr & MM_PAGE_MASK, MMU_PTE_FLAGS) != 0) {
			mem_free_page(page);
			return -1;
		}

		return 0;
	}
	// _trace("Wront type", "");
	return -1;
}
