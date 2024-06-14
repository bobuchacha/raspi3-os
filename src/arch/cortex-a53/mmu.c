#include "ros.h"
#include "printf.h"
#include "log.h"
#include "arch/cortex-a53/mmu.h"
#include "task.h"
#include "memory.h"
#include "device.h"

#define MM_TYPE_PAGE_TABLE		0x3
#define MM_TYPE_PAGE 			0x3
#define MM_TYPE_BLOCK			0x1
#define MM_ACCESS			(0x1 << 10)
#define MM_ACCESS_PERMISSION		(0x01 << 6) 

/*
 * Memory region attributes:
 *
 *   n = AttrIndx[2:0]
 *			n	MAIR
 *   DEVICE_nGnRnE	000	00000000
 *   NORMAL_NC		001	01000100
 */
#define MT_DEVICE_nGnRnE 		0x0
#define MT_NORMAL_NC			0x1
#define MT_DEVICE_nGnRnE_FLAGS		0x00
#define MT_NORMAL_NC_FLAGS  		0x44
#define MAIR_VALUE			(MT_DEVICE_nGnRnE_FLAGS << (8 * MT_DEVICE_nGnRnE)) | (MT_NORMAL_NC_FLAGS << (8 * MT_NORMAL_NC))

#define MMU_FLAGS	 		(MM_TYPE_BLOCK | (MT_NORMAL_NC << 2) | MM_ACCESS)	
#define MMU_DEVICE_FLAGS		(MM_TYPE_BLOCK | (MT_DEVICE_nGnRnE << 2) | MM_ACCESS)	
#define MMU_PTE_FLAGS			(MM_TYPE_PAGE | (MT_NORMAL_NC << 2) | MM_ACCESS | MM_ACCESS_PERMISSION)	


void _map_table_entry(TableEntry *pte, Address va, Address pa, Flags flags) {
	unsigned long index = va >> MM_PAGE_SHIFT;
	index = index & (MM_PTRS_PER_TABLE - 1);
	// _tracef(">>>>>>>>>> Mapping PTE: VA: 0x%lX Index: %d\n", va, index);
	// unsigned long entry = pa | flags | PT_BLOCK_ENTRY;      // this is a block 
	unsigned long entry = pa | flags | 3;      // this is a block 
	pte[index] = entry;
}

Address _map_table(TableEntry *table, ULong shift, Address va, Bool* is_table_new) {
	// _tracef("mapping table 0x%x", table);
	unsigned long index = va >> shift;
	// _tracef(">>>>>>>>>> Mapping Directory: VA: 0x%lX >> %d Index: %d\n", va,shift, index);
	index = index & (MM_PTRS_PER_TABLE - 1);
	if (!table[index]){
		*is_table_new = true;
		Address next_level_table = (Address)mem_alloc_page();
		TableEntry entry = (ULong)next_level_table | PT_TABLE_ENTRY;
		table[index] = entry;
		return next_level_table;
	} else {
		*is_table_new = 0;
	}
	return table[index] & MM_PAGE_MASK;
}

/**
 * map physical address into process' virtual memory map
*/
void process_map_page(Task *task, Address pa, Address va, Flags flags) {
    Address pgd;
	
    // _trace("Mapping process memory 0x%lX to 0x%lX\n", pa, va);

    // create PGD if task doesnt have it yet
    if (!task->mm.pgd) {
		// _trace("Creating new page for PGD...\n");
		task->mm.pgd = (Pointer)mem_alloc_page();
		task->mm.kernel_pages[++task->mm.kernel_pages_count] = task->mm.pgd;
	}
	pgd = (Address)task->mm.pgd;
    // kdebug("This PGD is at 0x%lX", pgd);
    // check pud
	Bool is_table_new;
	
	Address pud = _map_table((TableEntry*)(pgd + VA_START), MM_PGD_SHIFT, va, &is_table_new);
	if (is_table_new) {
		// _trace("Created new page for PUD...\n");
		task->mm.kernel_pages[++task->mm.kernel_pages_count] = (Pointer)pud;
	}

    // check pmd
	Address pmd = _map_table((TableEntry *)(pud + VA_START) , MM_PUD_SHIFT, va, &is_table_new);
	if (is_table_new) {
		// _trace("Created new page for PMD...\n");
		task->mm.kernel_pages[++task->mm.kernel_pages_count] = (Pointer)pmd;
	}

    // check for correspondent pte
	Address pte = _map_table((TableEntry *)(pmd + VA_START), MM_PMD_SHIFT, va, &is_table_new);
	if (is_table_new) {
		// _trace("Created new page for PTE...\n");
		task->mm.kernel_pages[++task->mm.kernel_pages_count] = (Pointer)pte;
	}

    // now we got all tables we need for specific va, let's map the 4K entry
	_map_table_entry((TableEntry *)(pte + VA_START), va, pa, flags);

	UserPage p = {pa, va};
	// _trace("Mapped 0x%lX to 0x%lX", pa, va);
	task->mm.user_pages[task->mm.user_pages_count++] = p;
}

unsigned long mem_handle_data_abort(ulong addr, ulong esr){
	_trace("Called by process %d. Requested access to address 0x%lX\n", current_task->id, addr);
	unsigned long dfs = (esr & 0b111111);
	if ((dfs & 0b111100) == 0b100) {
		Address page = mem_alloc_page();
		if (!page) {
			_trace("Page not allocated");
			return -1;
		}
		process_map_page(current_task, page, (Address)addr & MM_PAGE_MASK, MMU_PTE_FLAGS);

		return 0;
	}
	// _trace("Wront type", "");
	return -1;
}