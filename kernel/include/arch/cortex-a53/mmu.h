#ifndef ROS_ARCH_CORTEX_A53_MMU_H
#define ROS_ARCH_CORTEX_A53_MMU_H

#include "mmu/mair.h"
#include "mmu/pte.h"
#include "mmu/tcr.h"

/* Translate generic user-memory intents into this architecture's page-table flags. */

// Memory Type: User Code
#define MT_USER_CODE PE_USER_CODE // user code
#define MT_USER_DATA PE_USER_DATA // user data
#define MT_USER_RO PE_USER_RO     // user read-only
#define MT_USER_RW PE_USER_RW     // user read-write

typedef unsigned long TableEntry;

#ifndef __ASSEMBLER__

#include "memory.h"
#include "task.h"

typedef struct MmuWalkResult {
	Bool valid;
	VirtAddr virt_addr;
	PhysAddr phys_addr;
	Address pgd;
	Address pud;
	Address pmd;
	Address pte;
	ULong pgd_index;
	ULong pud_index;
	ULong pmd_index;
	ULong pte_index;
	TableEntry entry;
	Flags flags;
} MmuWalkResult;

/**
 * Map one owned physical page into a task user address space.
 *
 * Args:
 *   task: Task receiving the mapping.
 *   pa: Page-aligned physical page base.
 *   va: Page-aligned user virtual address.
 *   flags: Architecture-specific page attributes.
 *
 * Returns:
 *   `0` on success, or `-1` when validation, tracking, or page-table allocation fails.
 */
int process_map_page(Task *task, Address pa, Address va, Flags flags);

/**
 * Map an already-owned physical page into a task without transferring free ownership to that task.
 *
 * Args:
 *   task: Task whose user page tables are updated.
 *   pa: Physical page base to map.
 *   va: User virtual address where the page will appear.
 *   flags: Architecture-specific page attributes.
 *
 * Returns:
 *   Nothing. The mapping is tracked as shared so task cleanup will not free `pa`.
 */
int process_map_shared_page(Task *task, Address pa, Address va, Flags flags);

/**
 * Remove one user page mapping from a task and free its backing page.
 *
 * Args:
 *   task: Task that owns the mapping.
 *   va: Virtual address inside the page to remove.
 *
 * Returns:
 *   `0` on success, or `-1` when the mapping does not exist.
 */
int process_unmap_page(Task *task, Address va);

/**
 * Replace the physical backing page and flags for an existing task mapping.
 *
 * Args:
 *   task: Task that owns the mapping.
 *   pa: New page-aligned physical page base.
 *   va: Existing page-aligned user virtual address.
 *   flags: New architecture-specific page attributes.
 *   tracking_flags: Ownership metadata for the replacement page.
 *
 * Returns:
 *   `0` on success, or `-1` when the mapping does not exist or validation fails.
 */
int process_remap_page(Task *task, Address pa, Address va, Flags flags, UInt tracking_flags);

/**
 * Update only the access flags of an existing user mapping.
 *
 * Args:
 *   task: Task that owns the mapping.
 *   va: Existing page-aligned user virtual address.
 *   flags: Replacement architecture-specific page attributes.
 *
 * Returns:
 *   `0` on success, or `-1` when the mapping does not exist.
 */
int process_update_page_flags(Task *task, Address va, Flags flags);

/**
 * Walk the task page tables for one virtual address.
 *
 * Args:
 *   task: Task whose page tables will be inspected.
 *   va: Virtual address to resolve.
 *   result: Output structure filled with table addresses, indices, and final entry data.
 *
 * Returns:
 *   `0` when a valid mapping exists, or `-1` when any level is missing.
 */
int process_walk_page(Task *task, Address va, MmuWalkResult *result);

/**
 * Copy bytes from a task's user virtual address space into a kernel buffer.
 *
 * Args:
 *   task: Task whose address space will be read.
 *   va: Starting user virtual address.
 *   buffer: Kernel destination buffer.
 *   len: Number of bytes to copy.
 *
 * Returns:
 *   `0` on success, or `-1` when any page in the range is unmapped.
 */
int process_copy_from_user(Task *task, Address va, void *buffer, ULong len);

/**
 * Copy bytes from a kernel buffer into a task's user virtual address space.
 *
 * Args:
 *   task: Task whose address space will be written.
 *   va: Starting user virtual address.
 *   buffer: Kernel source buffer.
 *   len: Number of bytes to copy.
 *
 * Returns:
 *   `0` on success, or `-1` when any page in the range is unmapped.
 */
int process_copy_to_user(Task *task, Address va, const void *buffer, ULong len);

/**
 * Print the tracked user mappings for a task along with their resolved PTE state.
 *
 * Args:
 *   task: Task to inspect.
 *
 * Returns:
 *   Number of mappings printed, or `-1` when `task` is invalid.
 */
int process_dump_mappings(Task *task);

/**
 * Run a lightweight MMU self-test covering map, walk, remap, flag update, and unmap.
 *
 * Args:
 *   None.
 *
 * Returns:
 *   `0` on success, or `-1` when any MMU invariant fails.
 */
int process_mmu_self_test(void);

#endif

#endif