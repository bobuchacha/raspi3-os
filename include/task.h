#ifndef TASK_H
#define TASK_H

#define THREAD_CPU_CONTEXT            0        // offset of cpu_context in task_struct

#ifndef __ASSEMBLER__

#include <ros.h>

typedef enum TASK_STATES {
    TASK_RUNNING = 1,
    TASK_ZOMBIE  = 0
} TaskState;

enum TASK_PRIORITY {
    PRIORITY_NORMAL    = 1,
    PRIORITY_MEDIUM    = 2,
    PRIORITY_HIGH      = 3,
    PRIORITY_REAL_TIME = 4
};

typedef struct cpu_context {
    unsigned long x19;
    unsigned long x20;
    unsigned long x21;
    unsigned long x22;
    unsigned long x23;
    unsigned long x24;
    unsigned long x25;
    unsigned long x26;
    unsigned long x27;
    unsigned long x28;
    unsigned long fp;       // x29
    unsigned long sp;
    unsigned long pc;       // x30

} CPUContext;

#define MAX_PROCESS_PAGES            32

/**
 * metadata of a user page in our task memory management for easily clean up
*/
typedef struct user_page {
    Address phys_addr;
    Address virt_addr;
} UserPage;

typedef struct mm_struct {
    Pointer       pgd;
    int           user_pages_count;
    UserPage      user_pages[MAX_PROCESS_PAGES];
    int           kernel_pages_count;
    Pointer       kernel_pages[MAX_PROCESS_PAGES];
} TaskMemory;

/**
 * a register used to initialize thecpu registers before KERNEL_EXIT is called.
 * It should align with KERNEL_EXIT in the assembly code
*/
typedef struct pt_regs {
    unsigned long regs[31];
    unsigned long sp;
    unsigned long pc;
    unsigned long pstate;
} TaskRegisters;

/**
 * A structure of our task. include CPU Context and other information of the task
*/
typedef struct task_struct {
    CPUContext    cpu_context;
    long          id;
    TaskState     state;
    long          counter;
    long          priority;
    long          preempt_count;
    Flags          flags;
    TaskMemory    mm;
    Buffer        name;
} Task;


/*
 * THEAD CONFIG
 */
#define THREAD_SIZE                4096
#define NR_TASKS                64
#define FIRST_TASK              tasks[0]
#define LAST_TASK               tasks[nr_tasks - 1]
#define PF_KTHREAD                0x00000002

/*
 * PSR bits
 */
#define PSR_MODE_EL0t    0x00000000
#define PSR_MODE_EL1t    0x00000004
#define PSR_MODE_EL1h    0x00000005
#define PSR_MODE_EL2t    0x00000008
#define PSR_MODE_EL2h    0x00000009
#define PSR_MODE_EL3t    0x0000000c
#define PSR_MODE_EL3h    0x0000000d

/*
 * Let these variable expose everywhere through out our kernel code
 */
extern struct task_struct *current_task;
extern struct task_struct *tasks[NR_TASKS];         // max support running task
extern int                nr_tasks;               // number of running tasks

extern void init_schedler(void);

extern void schedler_schedule(void);

extern void schedler_timer_tick(void);

extern void schedler_switch_to(struct task_struct *next);

extern void preempt_disable(void);

extern void preempt_enable(void);

extern void cpu_switch_to(struct task_struct *prev, struct task_struct *next);

int copy_process(Flags clone_flags, unsigned long fn, unsigned long arg, unsigned long stack);


#define INIT_TASK \
/*cpu_context*/    { {0,0,0,0,0,0,0,0,0,0,0,0,0}, \
/* id */    0, \
/* state */ TASK_RUNNING,0,1,0, PF_KTHREAD, \
/* mm */    {0, 0, {0}, 0, {0}}, \
/* name */ "KERNEL IDLE" \
}   // this is our kernel task

struct pt_regs *task_pt_regs(struct task_struct *tsk);

void exit_current_process(long result);
void cleanup_zombie_processes();
void user_process_loader(Address program_addr, ulong program_size);
int create_user_process(Address program_addr, ULong program_size);
ulong process_copy_thread(Flags flags, Address program_addr, Pointer arg);
void process_dump_task_struct(Task* task);
void process_dump_task(ulong pid);
void dump_current_task();
void print_current_task_id();
void process_unload(Task *task);

#endif // __ASSEMBLER__
#endif // _TASK_H