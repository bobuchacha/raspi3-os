#include "ros.h"
#include "printf.h"
#include "device.h"
#include "task.h"
#include "irq.h"            // some code in device irq or arch irq
#include "log.h"
#include "memory.h"
#define PAGE_MASK			0xfffffffffffff000
#define PAGE_SHIFT	 		12
#define TABLE_SHIFT 			9
#define SECTION_SHIFT			(PAGE_SHIFT + TABLE_SHIFT)

#define MM_TYPE_PAGE_TABLE		0x3
#define MM_TYPE_PAGE 			0x3
#define MM_TYPE_BLOCK			0x1
#define MM_ACCESS			(0x1 << 10)
#define MM_ACCESS_PERMISSION		(0x01 << 6) 

#define MT_DEVICE_nGnRnE 		0x0
#define MT_NORMAL_NC			0x1
#define MT_DEVICE_nGnRnE_FLAGS		0x00
#define MT_NORMAL_NC_FLAGS  		0x44
#define MAIR_VALUE			(MT_DEVICE_nGnRnE_FLAGS << (8 * MT_DEVICE_nGnRnE)) | (MT_NORMAL_NC_FLAGS << (8 * MT_NORMAL_NC))


#define PAGE_SIZE   			(1 << PAGE_SHIFT)	
#define SECTION_SIZE			(1 << SECTION_SHIFT)	

#define LOW_MEMORY              	(2 * SECTION_SIZE)
#define HIGH_MEMORY             	DEVICE_BASE

#define PAGING_MEMORY 			(HIGH_MEMORY - LOW_MEMORY)
#define PAGING_PAGES 			(PAGING_MEMORY/PAGE_SIZE)

#define PTRS_PER_TABLE			(1 << TABLE_SHIFT)

#define PGD_SHIFT			PAGE_SHIFT + 3*TABLE_SHIFT
#define PUD_SHIFT			PAGE_SHIFT + 2*TABLE_SHIFT
#define PMD_SHIFT			PAGE_SHIFT + TABLE_SHIFT

#define MM_TYPE_PAGE_TABLE		0x3
#define MMU_FLAGS	 		(MM_TYPE_BLOCK | (MT_NORMAL_NC << 2) | MM_ACCESS)	
#define MMU_DEVICE_FLAGS		(MM_TYPE_BLOCK | (MT_DEVICE_nGnRnE << 2) | MM_ACCESS)	
#define MMU_PTE_FLAGS			(MM_TYPE_PAGE | (MT_NORMAL_NC << 2) | MM_ACCESS | MM_ACCESS_PERMISSION)	


#define _trace          log_info
#define _trace_printf   printf

extern unsigned long ret_from_fork();

void copy_virtual_memory(struct task_struct *p);

/**
 * copy a process
*/
int copy_process(unsigned long clone_flags, unsigned long fn, unsigned long arg, unsigned long stack)
{
	preempt_disable();
	struct task_struct *p;

	void *page = mem_alloc_page();
	if (!page) return -1;

	p = (struct task_struct *) page;
	struct pt_regs *childregs = task_pt_regs(p);

	_trace("Copying new process\n");

	// memzero((unsigned long)childregs, sizeof(struct pt_regs));
	// memzero((unsigned long)&p->cpu_context, sizeof(struct cpu_context));

	if (clone_flags & PF_KTHREAD) {
		p->cpu_context.x19 = fn;
		p->cpu_context.x20 = arg;
	} else {
		struct pt_regs * cur_regs = task_pt_regs(current_task);
		*childregs = *cur_regs;
		childregs->regs[0] = 0;
		// childregs->sp = stack + PAGE_SIZE;
		// p->stack = stack;
		copy_virtual_memory(p);		// copy virtual memory from current task to the new process
	}


	p->flags = clone_flags;
	p->priority = current_task->priority;
	p->state = TASK_RUNNING;
	p->counter = p->priority;
	p->preempt_count = 1; //disable preemtion until schedule_tail

	p->cpu_context.pc = (unsigned long)ret_from_fork;
	p->cpu_context.sp = (unsigned long)childregs;
	
	int pid = ++nr_tasks;
	p->id = pid;
	_trace_printf("Number of numming task: %d\n", pid);
	tasks[pid] = p;

	preempt_enable();
	return pid;
}

void map_table_entry(unsigned long *pte, unsigned long va, unsigned long pa) {
	unsigned long index = va >> PAGE_SHIFT;
	index = index & (PTRS_PER_TABLE - 1);
	unsigned long entry = pa | MMU_PTE_FLAGS; 
	pte[index] = entry;
}

unsigned long map_table(unsigned long *table, unsigned long shift, unsigned long va, int* new_table) {
	unsigned long index = va >> shift;
	index = index & (PTRS_PER_TABLE - 1);
	if (!table[index]){
		*new_table = 1;
		unsigned long next_level_table = mem_alloc_page();
		unsigned long entry = next_level_table | MM_TYPE_PAGE_TABLE;
		table[index] = entry;
		return next_level_table;
	} else {
		*new_table = 0;
	}
	return table[index] & PAGE_MASK;
}

void map_page(struct task_struct *task, unsigned long va, unsigned long page){
	unsigned long pgd;
	if (!task->mm.pgd) {
		task->mm.pgd = mem_alloc_page();
		task->mm.kernel_pages[++task->mm.kernel_pages_count] = task->mm.pgd;
	}
	pgd = task->mm.pgd;
	int new_table;
	unsigned long pud = map_table((unsigned long *)(pgd + VA_START), PGD_SHIFT, va, &new_table);
	if (new_table) {
		task->mm.kernel_pages[++task->mm.kernel_pages_count] = pud;
	}
	unsigned long pmd = map_table((unsigned long *)(pud + VA_START) , PUD_SHIFT, va, &new_table);
	if (new_table) {
		task->mm.kernel_pages[++task->mm.kernel_pages_count] = pmd;
	}
	unsigned long pte = map_table((unsigned long *)(pmd + VA_START), PMD_SHIFT, va, &new_table);
	if (new_table) {
		task->mm.kernel_pages[++task->mm.kernel_pages_count] = pte;
	}
	map_table_entry((unsigned long *)(pte + VA_START), va, page);
	struct user_page p = {page, va};
	task->mm.user_pages[task->mm.user_pages_count++] = p;
}


unsigned long allocate_user_page(struct task_struct *task, unsigned long va) {
	unsigned long page = mem_alloc_page();
	if (page == 0) {
		return 0;
	}
	map_page(task, va, page);
	return page + VA_START;
}

void copy_virtual_memory(struct task_struct *dst){
	struct task_struct* src = current_task;
	for (int i = 0; i < src->mm.user_pages_count; i++) {
		unsigned long kernel_va = allocate_user_page(dst, src->mm.user_pages[i].virt_addr);
		if( kernel_va == 0) {
			return -1;
		}
		memcpy(kernel_va, src->mm.user_pages[i].virt_addr, PAGE_SIZE);
	}
	_trace("Copying virtual memory...");
	return 0;
}

/**
 * our pt_regs are right above the stack
*/
struct pt_regs * task_pt_regs(struct task_struct *tsk){
	unsigned long p = (unsigned long)tsk + THREAD_SIZE - sizeof(struct pt_regs);
	return (struct pt_regs *)p;
}

int move_to_user_mode(unsigned long start, unsigned long size, unsigned long pc)
{
	struct pt_regs *regs = task_pt_regs(current_task);
	regs->pstate = PSR_MODE_EL0t;
	regs->pc = pc;
	regs->sp = 2 *  PAGE_SIZE;  
	unsigned long code_page = allocate_user_page(current_task, 0);
	if (code_page == 0)	{
		return -1;
	}
	memcpy(code_page, start, size);
	set_pgd(current_task->mm.pgd);
	return 0;
}
