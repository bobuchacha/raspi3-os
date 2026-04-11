#include "input.h"

#include "arch/cortex-a53/dbg.h"
#include "device.h"
#include "task.h"
#include "log.h"

static char g_input_queue[INPUT_QUEUE_CAPACITY];
static unsigned int g_input_queue_head;
static unsigned int g_input_queue_tail;
static unsigned int g_input_queue_count;
static volatile unsigned int g_input_queue_lock;
static Task* g_input_waiter;

static inline unsigned int input_spin_try_acquire(volatile unsigned int* lock) {
    unsigned int previous;
    unsigned int status;

    asm volatile(
        "ldaxr %w0, [%2]\n"
        "cbnz %w0, 1f\n"
        "mov %w0, #1\n"
        "stxr %w1, %w0, [%2]\n"
        "cbnz %w1, 2f\n"
        "mov %w0, wzr\n"
        "b 3f\n"
        "1:\n"
        "mov %w1, wzr\n"
        "2:\n"
        "3:\n"
        : "=&r"(previous), "=&r"(status)
        : "r"(lock)
        : "memory");

    return previous == 0U && status == 0U;
}

static inline void input_spin_acquire(volatile unsigned int* lock) {
    while (!input_spin_try_acquire(lock)) {
        asm volatile("yield\n");
    }
}

static inline void input_spin_release(volatile unsigned int* lock) {
    asm volatile(
        "stlr %w1, [%0]\n"
        :
    : "r"(lock), "r"(0U)
        : "memory");
}

void input_init(void) {
    _trace("input_init");
    input_spin_acquire(&g_input_queue_lock);
    g_input_queue_head = 0U;
    g_input_queue_tail = 0U;
    g_input_queue_count = 0U;
    g_input_waiter = 0;
    input_spin_release(&g_input_queue_lock);
}

static void input_wake_waiter(void) {
    Task* waiter;

    input_spin_acquire(&g_input_queue_lock);
    waiter = g_input_waiter;
    if (waiter && waiter->state != TASK_BLOCKED) {
        g_input_waiter = 0;
        waiter = 0;
    }
    if (waiter) {
        g_input_waiter = 0;
    }
    input_spin_release(&g_input_queue_lock);

    if (waiter) {
        (void)schedler_unblock_task(waiter);
    }
}

void input_poll(void) {
    char key;

    while (uart0_try_getc(&key) == 0) {
        if (input_enqueue_key((unsigned long)(unsigned char)key) != 0) {
            break;
        }
    }
}

int input_enqueue_key(unsigned long key) {
    char normalized;
    int status = 0;

    if (key > 0xFFUL) {
        return -1;
    }

    normalized = (char)(unsigned char)key;
    if (normalized == '\r') {
        normalized = '\n';
    }

    input_spin_acquire(&g_input_queue_lock);
    if (g_input_queue_count >= INPUT_QUEUE_CAPACITY) {
        status = -1;
    }
    else {
        g_input_queue[g_input_queue_head] = normalized;
        g_input_queue_head = (g_input_queue_head + 1U) % INPUT_QUEUE_CAPACITY;
        g_input_queue_count++;
    }
    input_spin_release(&g_input_queue_lock);

    if (status == 0) {
        input_wake_waiter();
    }

    return status;
}

int input_try_dequeue_key(char* out_char) {
    int status = -1;

    if (!out_char) {
        return -1;
    }

    input_spin_acquire(&g_input_queue_lock);
    if (g_input_queue_count > 0U) {
        *out_char = g_input_queue[g_input_queue_tail];
        g_input_queue_tail = (g_input_queue_tail + 1U) % INPUT_QUEUE_CAPACITY;
        g_input_queue_count--;
        status = 0;
    }
    input_spin_release(&g_input_queue_lock);

    return status;
}

long input_read_key(void) {
    char key;

    for (;;) {
        input_poll();

        if (input_try_dequeue_key(&key) == 0) {
            return (unsigned long)(unsigned char)key;
        }

        dbg_wait_if_paused();

        input_spin_acquire(&g_input_queue_lock);
        if (g_input_waiter == 0 || g_input_waiter == current_task) {
            g_input_waiter = current_task;
        }
        input_spin_release(&g_input_queue_lock);

        schedler_block_current();
    }
}