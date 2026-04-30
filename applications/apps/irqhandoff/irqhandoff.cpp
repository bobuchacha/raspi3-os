#include "app/app.h"

#define IRQHANDOFF_WORKER_COUNT 1UL
#define IRQHANDOFF_HEARTBEAT_MASK 0x01FFFFFFUL

static volatile unsigned long g_irqhandoff_worker_progress[IRQHANDOFF_WORKER_COUNT];

/*
 * irqhandoff_text_equals
 *
 * The shell passes a flat task-argument string into each userspace program.
 * The validation probe only needs one tiny exact-match check, so a private
 * helper keeps the `solo` mode self-contained without pulling in heavier
 * parsing code.
 *
 * @param left First string.
 * @param right Second string.
 * @return Non-zero when both strings are identical.
 */
static int irqhandoff_text_equals(const char* left, const char* right) {
    unsigned long index;

    if (left == right) {
        return 1;
    }
    if (left == 0 || right == 0) {
        return 0;
    }

    for (index = 0UL;; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
        if (left[index] == '\0') {
            return 1;
        }
    }
}

/*
 * irqhandoff_should_run_solo
 *
 * The no-switch validation needs one EL0 thread that stays CPU-bound without
 * starting a peer worker. Reusing the same binary keeps the proof workload easy
 * to stage from the shell while still separating the solo and handoff cases.
 *
 * @return Non-zero when the task arguments request `solo` mode.
 */
static int irqhandoff_should_run_solo(void) {
    char args[64];

    args[0] = '\0';
    if (getTaskArgs(args, sizeof(args)) < 0) {
        return 0;
    }

    return irqhandoff_text_equals(args, "solo");
}

/*
 * Publish one worker progress snapshot.
 *
 * The busy-loop body keeps its running accumulator local for throughput and
 * only stores into the shared volatile array at coarse checkpoints so the
 * optimizer cannot collapse the loop away while still leaving both EL0 threads
 * effectively CPU-bound.
 *
 * @param worker_index Zero-based worker slot.
 * @param progress Latest accumulator value to publish.
 * @return Nothing.
 */
static void irqhandoff_publish_progress(unsigned long worker_index, unsigned long progress) {
    if (worker_index >= IRQHANDOFF_WORKER_COUNT) {
        return;
    }

    g_irqhandoff_worker_progress[worker_index] = progress;
}

/*
 * Run one intentionally non-cooperative CPU worker.
 *
 * The remaining proof gap is not interrupt delivery but proving the scheduler
 * actually switches away from one EL0 thread when the timer IRQ lands. This
 * worker is the second runnable peer beside the process main thread, and both
 * threads stay CPU-bound at the same highest priority so the worker can only
 * come online through timer-driven preemption.
 *
 * @param argument One-based worker identifier supplied by `startUserThread`.
 * @return Nothing.
 */
static void irqhandoff_worker_entry(unsigned long argument) {
    unsigned long worker_id = argument;
    unsigned long worker_index;
    unsigned long spin_state;

    if (worker_id == 0UL || worker_id > IRQHANDOFF_WORKER_COUNT) {
        debugError("irqhandoff: invalid worker id=%lu", worker_id);
        return;
    }

    worker_index = worker_id - 1UL;
    spin_state = 0x13579BDFUL ^ (worker_id * 0x01010101UL);
    debugInfo("irqhandoff: worker %lu online", worker_id);

    for (;;) {
        spin_state = (spin_state * 1664525UL) + 1013904223UL + worker_id;
        irqhandoff_publish_progress(worker_index, spin_state);
        if ((spin_state & IRQHANDOFF_HEARTBEAT_MASK) == 0UL) {
            debugInfo(
                "irqhandoff: worker %lu heartbeat progress=%lu",
                worker_id,
                g_irqhandoff_worker_progress[worker_index]);
        }
    }
}

/*
 * irqhandoff_run_solo_probe
 *
 * This mode exists only to validate that the lower-EL timer IRQ path is live
 * even when the scheduler keeps the interrupted thread running. A single busy
 * EL0 thread should still trigger the IRQ entry trace plus the scheduler's
 * `lower-el timer resume-current` proof without ever needing a second ready
 * peer.
 *
 * @return Zero because the probe intentionally runs forever.
 */
static int irqhandoff_run_solo_probe(void) {
    unsigned long spin_state;

    spin_state = 0x5A17C3E1UL;
    debugInfo("irqhandoff: solo mode keeps one EL0 thread runnable waiting for timer resume-current proof");
    for (;;) {
        spin_state = (spin_state * 214013UL) + 2531011UL;
        if ((spin_state & IRQHANDOFF_HEARTBEAT_MASK) == 0UL) {
            debugInfo("irqhandoff: solo heartbeat progress=%lu", spin_state);
        }
    }
}

/*
 * Start the two-worker handoff probe.
 *
 * The main thread is intentionally the first runnable peer in the proof. After
 * it raises one worker to the same highest priority, it enters its own busy
 * loop without sleeping or yielding. If the worker ever prints its online
 * message, that handoff can only have happened because the timer IRQ preempted
 * the still-runnable main thread.
 *
 * @return Zero on success, or one when thread startup failed.
 */
int main(void) {
    long first_thread_id;
    unsigned long spin_state;

    if (irqhandoff_should_run_solo()) {
        return irqhandoff_run_solo_probe();
    }

    debugInfo("irqhandoff: starting two-thread IRQ handoff probe");
    g_irqhandoff_worker_progress[0] = 0UL;
    spin_state = 0x2468ACE0UL;

    first_thread_id = startUserThread((unsigned long)&irqhandoff_worker_entry, 1UL, "irqhandoff.1");
    if (first_thread_id < 0L) {
        debugError("irqhandoff: failed to start worker 1 status=%ld", first_thread_id);
        return 1;
    }

    debugInfo(
        "irqhandoff: worker started tid1=%ld; both threads stay runnable waiting for timer-driven handoff",
        first_thread_id);
    for (;;) {
        spin_state = (spin_state * 1103515245UL) + 12345UL;
        if ((spin_state & IRQHANDOFF_HEARTBEAT_MASK) == 0UL) {
            debugInfo(
                "irqhandoff: main heartbeat progress=%lu worker1=%lu",
                spin_state,
                g_irqhandoff_worker_progress[0]);
        }
    }
}