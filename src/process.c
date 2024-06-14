#include "ros.h"
#include "device.h"
#include "memory.h"
#include "printf.h"
#include "log.h"
#include "task.h"
#include "utils.h"

// entry.S
extern unsigned long ret_from_fork();
extern unsigned long user_process_pre_loader();

void process_dump_task_struct(Task* task);
void copy_virtual_memory(struct task_struct *p);

//mmu.c
void process_map_page(Task *task, ulong pa, ulong va, ulong flags);

/**
 * unload program, clean the pages. 
 * Always return null.
*/
void process_unload(Task *task){
    preempt_disable();
    // clear user pages

    // unset task in the array
    tasks[task->id] = null;

    task->state = TASK_ZOMBIE;
    
    // remove kernel pages
    for (int j = 0; j < task->mm.kernel_pages_count; j++) {
        // _trace("Freeing page 0x%lX", task->mm.kernel_pages[j]);
        mem_free_page((Address)task->mm.kernel_pages[j]);
    }

    // free user pages
    for (int j = 0; j < task->mm.user_pages_count; j++) {
        // _trace("Freeing user page 0x%lX", task->mm.user_pages[j].phys_addr);
        mem_free_page((Address)task->mm.user_pages[j].phys_addr);
    }

	preempt_enable();
	schedler_schedule();
}

void exit_current_process(long result){
    _trace("Process %d exitted with status 0x%lx", current_task->id, result);
    process_unload(current_task);
}

void cleanup_zombie_processes(){
    // _trace("Cleaning up...");
    preempt_disable();
    Task* t;
    for (int i = 0; i < NR_TASKS; i++){
        t = tasks[i];
        if (!t) continue;
        if (t->state == TASK_ZOMBIE){
            _trace("Cleaning up task %d", i);

            // remove kernel pages
            for (int j = 0; j < t->mm.kernel_pages_count; j++) {
                _trace("Freeing page 0x%lX", t->mm.kernel_pages[j]);
                mem_free_page((Address)t->mm.kernel_pages[j]);
            }

            // free user pages
            for (int j = 0; j < t->mm.user_pages_count; j++) {
                _trace("Freeing user page 0x%lX", t->mm.user_pages[j].phys_addr);
                mem_free_page((Address)t->mm.user_pages[j].phys_addr);
            }

            // unset task in the array
            tasks[i] = null;
        }
    }
    preempt_enable();

}

/**
 * this process is called from ret_from_fork when CPU is switched here
 * x20 = program_addr
 * x21 = program_size
*/
void user_process_loader(Address program_addr, ulong program_size){
    _trace("Loading user process at 0x%lX, size: %d bytes...", program_addr, program_size);
    preempt_disable();
    
    // prepare our thread to switch to user
    Task *task = current_task;
    int program_code_pages = (program_size / PAGE_SIZE) + 1,
        program_stack_pages = VA_USER_STACK / PAGE_SIZE;
    int i, code_offset, copy_length, j;

    task->mm.kernel_pages[0] = task;
    task->flags = 0;
    task->mm.pgd = 0;           // get away with kernel PGD

    // reserve memory for stack
    // only need to map 1 page. As the Data Abort handler will map more page as stack grows downward
    Address s = mem_alloc_page();
    if (!s) return process_unload(task);

    process_map_page(task, s, (Address)(VA_USER_STACK - PAGE_SIZE), PE_USER_DATA);
    task->mm.kernel_pages[++task->mm.kernel_pages_count] = (Pointer)s;

    // prepare our task registers that ret_to_user will use. These info will be loaded by kernel_exit 0
    struct pt_regs *regs = task_pt_regs(task);
	regs->pstate = PSR_MODE_EL0t;                   // change to EL0
	regs->pc = VA_USER_START;                       // our code start at this address. Period!
	regs->sp = VA_USER_STACK;                       // our stack start from here, grow down ward

    // reserve memory for code. And copy user code into its memory
    for (i = program_stack_pages;program_code_pages > 0; program_code_pages--, i++, j++) {
        Address _segment = mem_alloc_page();
        if (!_segment) return process_unload(task);
        
        code_offset = j * PAGE_SIZE;
        copy_length = (program_size - (j * PAGE_SIZE)) > PAGE_SIZE ? PAGE_SIZE : (program_size % PAGE_SIZE);

        // _trace("Allocating 1 page for the new process code...\n");
        process_map_page(task, _segment, i * PAGE_SIZE, PE_USER_CODE);          
        memcpy(_segment + VA_START, program_addr + code_offset, copy_length);        
    }

    // set pgd
    set_pgd(task->mm.pgd);
    preempt_enable();
    // _trace("User process loaded. process id: %d. Switching to user mode...", current_task->id);
    // while(1);

}

/**
 * run user program
*/
int create_user_process(Address program_addr, ULong program_size){
    preempt_disable();
	Task *new_task;

	Address page = mem_alloc_page();
	if (!page) return -1;

	new_task = (struct task_struct *) (page + VA_START);
	struct pt_regs *childregs = task_pt_regs(new_task);

	_trace("Forking new thread. Address: 0x%lx. SP: 0x%lX\n", (ulong)new_task, childregs);

    new_task->cpu_context.x19 = (ULong) &user_process_loader;
    new_task->cpu_context.x20 = (ULong)program_addr;
    new_task->cpu_context.x21 = program_size;
    new_task->mm.pgd = get_pgd();               // user kenel pgd for now

	new_task->flags = PF_KTHREAD;
	new_task->priority = current_task->priority;
	new_task->state = TASK_RUNNING;
	new_task->counter = new_task->priority;
	new_task->preempt_count = 1; //disable preemtion until schedule_tail

	// new_task->cpu_context.pc = (unsigned long)user_process_pre_loader;
	new_task->cpu_context.pc = (unsigned long)ret_from_fork;
	new_task->cpu_context.sp = (unsigned long)childregs;    // new thread's SP is right below the reg struct. This is kernel space address
	
	int pid = nr_tasks++;
	new_task->id = pid;
	tasks[pid] = new_task;

	preempt_enable();
	return pid;
}



/**
 * fork current process and create a separate thread
*/
ulong process_copy_thread(Flags flags, Address program_addr, Pointer arg){
    preempt_disable();
	Task *new_task;

	Address page = mem_alloc_page();
	if (!page) return -1;

	new_task = (struct task_struct *) (page + VA_START);
	struct pt_regs *childregs = task_pt_regs(new_task);

	_trace("Forking new thread. Address: 0x%lx. SP: 0x%lX\n", (ulong)new_task, childregs);

	// memzero((unsigned long)childregs, sizeof(struct pt_regs));
	// memzero((unsigned long)&p->cpu_context, sizeof(struct cpu_context));

	if (flags & PF_KTHREAD) {
		new_task->cpu_context.x19 = (ULong)program_addr;
		new_task->cpu_context.x20 = (ULong)arg;
        new_task->mm.pgd = get_pgd();
	} else {
        _trace("Copying new user thread");
        kerror("Implement this!");
		// struct pt_regs * cur_regs = task_pt_regs(current_task);
		// *childregs = *cur_regs;
		// childregs->regs[0] = 0;
		// // childregs->sp = stack + PAGE_SIZE;
		// // p->stack = stack;
		// copy_virtual_memory(new_task);		// copy virtual memory from current task to the new process
	}

	new_task->flags = flags;
	new_task->priority = current_task->priority;
	new_task->state = TASK_RUNNING;
	new_task->counter = new_task->priority;
	new_task->preempt_count = 1; //disable preemtion until schedule_tail

	new_task->cpu_context.pc = (ULong)ret_from_fork;
	new_task->cpu_context.sp = (ULong)childregs;    // new thread's SP is right below the reg struct
	
	int pid = nr_tasks++;
	new_task->id = pid;
	tasks[pid] = new_task;

	preempt_enable();
	return pid;
}

void process_dump_task_struct(Task* task){
    kprint("-------------------------------- [TASK INFO] ---------------------------------\n");

    kprint("  - Task ID:            %d\n", task->id);
    kprint("  - Counter:            %d\n", task->counter);
    kprint("  - Priority:           %d\n", task->priority);
    kprint("  - Preempt Count:      %d\n", task->preempt_count);
    kprint("  - State:              %d\n", task->state);
    kprint("  - Flags:              0x%X\n", task->flags);
    kprint("  Memory:\n");
    kprint("  - PGD:                0x%lX\n", task->mm.pgd);
    kprint("  - User Pages:         %d\n", task->mm.user_pages_count);
    kprint("  - Kernel Pages:       %d\n", task->mm.kernel_pages_count);
    kprint("  Registers:\n");
    kprint("  - x19: %16lX   x20: %16lX   x21: %16lX   \n", task->cpu_context.x19, task->cpu_context.x20, task->cpu_context.x21);
    kprint("  - x22: %16lX   x23: %16lX   x24: %16lX   \n", task->cpu_context.x22, task->cpu_context.x23, task->cpu_context.x24);
    kprint("  - x25: %16lX   x26: %16lX   x27: %16lX  \n", task->cpu_context.x25, task->cpu_context.x26, task->cpu_context.x27);
    kprint("  - x28: %16lX\n", task->cpu_context.x28);
    kprint("  - SP: %16lX   PC: %16lX   FP: %16lX   \n", task->cpu_context.sp, task->cpu_context.pc, task->cpu_context.fp);
    kprint("------------------------------------------------------------------------------\n");
}
void process_dump_task(ulong pid){
    process_dump_task_struct(tasks[pid]);
}

void dump_current_task(){
    process_dump_task_struct(current_task);
}
void print_current_task_id(){
    printf("==> CURRENT TASK ID: %d\n\n", current_task->id);
}
struct pt_regs *task_pt_regs(struct task_struct *tsk) {
    unsigned long p = (unsigned long)tsk + THREAD_SIZE - sizeof(struct pt_regs);
	return (struct pt_regs *)p;
}