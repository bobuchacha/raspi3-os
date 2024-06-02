#ifndef _TASK_H
#define _TASK_H

#define THREAD_CPU_CONTEXT			0 		// offset of cpu_context in task_struct 

#ifndef __ASSEMBLER__

#define THREAD_SIZE				4096
#define NR_TASKS				64 
#define FIRST_TASK              task[0]
#define LAST_TASK               task[NR_TASKS-1]

enum TASK_STATES {
    TASK_RUNNING = 1
};

enum TASK_PRIORITY {
    PRIORITY_NORMAL = 1,
    PRIORITY_MEDIUM = 2,
    PRIORITY_HIGH = 3,
    PRIORITY_REAL_TIME = 4
};

struct cpu_context {
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
     
};

struct task_struct {
    struct cpu_context cpu_context;
    long state;
    long counter;
    long priority;
    long preempt_count;
    long id;
};

extern struct task_struct *current_task;
extern struct task_struct *tasks[NR_TASKS];
extern int  nr_tasks;

extern void init_schedler(void);
extern void schedler_schedule(void);
extern void schedler_timer_tick(void);
extern void schedler_switch_to(struct task_struct* next);
extern void preempt_disable(void);
extern void preempt_enable(void);
extern void cpu_switch_to(struct task_struct* prev, struct task_struct* next);
int copy_process(unsigned long fn, unsigned long arg, int priority);

#define INIT_TASK \
/*cpu_context*/	{ {0,0,0,0,0,0,0,0,0,0,0,0,0}, \
/* state etc */	0,0,1, 0 \
}   // this is our kernel task

#endif // __ASSEMBLER__
#endif // _TASK_H