#ifndef ROS_LOADER_IRQ_H
#define ROS_LOADER_IRQ_H

static inline void enable_irq(void) {
    asm volatile("msr daifclr, #2" ::: "memory");
}

static inline void disable_irq(void) {
    asm volatile("msr daifset, #2" ::: "memory");
}

#endif
