#include "block_io.h"

#include "arch.h"
#include "heap.h"
#include "process.h"
#include "scheduler.h"
#include "thread.h"

struct BlockIoRequest {
    Device* device;
    U64 offset;
    void* buffer;
    Size length;
    SSize result;
    bool completed;
    Thread* waiting_thread;
    BlockIoRequest* next;
    BlockIoRequest* prev;
};

namespace {

    inline constexpr U8 BlockIoWorkerPriority = ThreadPriorityHighest;
    inline constexpr U64 BlockIoWaitRetryTicks = 1ULL;

    Process* g_block_io_worker_process;
    Thread* g_block_io_worker_thread;
    BlockIoRequest* g_block_io_request_head;
    BlockIoRequest* g_block_io_request_tail;

    /**
     * Enter the block-I/O service critical section.
     *
     * The first async read slice only needs short intrusive-queue mutations and
     * waiter handoffs, so disabling interrupts is the smallest lock that matches
     * the rest of the kernel service code and keeps request state transitions atomic.
     *
     * @return Previous interrupt-enabled state for later restoration.
     */
    bool block_io_lock(void) {
        return arch::Arch::save_and_disable_interrupts();
    }

    /**
     * Leave the block-I/O service critical section.
     *
     * @param interrupts_enabled Previous interrupt-enabled state from `block_io_lock()`.
     * @return Nothing.
     */
    void block_io_unlock(bool interrupts_enabled) {
        arch::Arch::restore_interrupts(interrupts_enabled);
    }

    /**
     * Release one heap-backed block-I/O request.
     *
     * Requests stay heap allocated so the worker, the waiter, and future query
     * paths can all observe the same stable state without borrowing a caller stack.
     *
     * @param request Request object to release.
     * @return Nothing.
     */
    void block_io_free_request(BlockIoRequest* request) {
        if (request != NULL) {
            Heap::free(request);
        }
    }

    /**
     * Append one request to the worker's pending queue.
     *
     * The queue is FIFO so the first async implementation preserves the same
     * simple ordering callers would have observed with direct serialized reads.
     *
     * @param request Prepared request to append.
     * @return Nothing.
     */
    void block_io_link_request(BlockIoRequest* request) {
        if (request == NULL) {
            return;
        }

        request->next = NULL;
        request->prev = g_block_io_request_tail;
        if (g_block_io_request_tail != NULL) {
            g_block_io_request_tail->next = request;
        }
        else {
            g_block_io_request_head = request;
        }
        g_block_io_request_tail = request;
    }

    /**
     * Pop the oldest queued block read.
     *
     * @return Detached request node, or NULL when the queue is empty.
     */
    BlockIoRequest* block_io_pop_request(void) {
        BlockIoRequest* request;
        bool interrupts_enabled;

        interrupts_enabled = block_io_lock();
        request = g_block_io_request_head;
        if (request != NULL) {
            g_block_io_request_head = request->next;
            if (g_block_io_request_head != NULL) {
                g_block_io_request_head->prev = NULL;
            }
            else {
                g_block_io_request_tail = NULL;
            }
            request->next = NULL;
            request->prev = NULL;
        }
        block_io_unlock(interrupts_enabled);
        return request;
    }

    /**
     * Complete one worker-owned request and wake its waiter when needed.
     *
     * Completion can race with the submitter still running before it actually
     * blocks. Waking only a thread already parked in `WaitReason::IO` avoids
     * enqueueing a still-running caller while still latching the completed result
     * for the later non-blocking `query()` or immediate `wait()` fast path.
     *
     * @param request Finished request node.
     * @param result Raw device-read result from the backend driver.
     * @return Nothing.
     */
    void block_io_complete_request(BlockIoRequest* request, SSize result) {
        Thread* waiter_to_wake = NULL;
        bool interrupts_enabled;

        if (request == NULL) {
            return;
        }

        interrupts_enabled = block_io_lock();
        request->result = result;
        request->completed = true;
        if ((request->waiting_thread != NULL)
            && (request->waiting_thread->current_state == ThreadState::Waiting)
            && (request->waiting_thread->wait_status == static_cast<U32>(WaitReason::IO))) {
            waiter_to_wake = request->waiting_thread;
            request->waiting_thread = NULL;
        }
        block_io_unlock(interrupts_enabled);

        if (waiter_to_wake != NULL) {
            (void)Scheduler::enqueue(waiter_to_wake);
        }
    }

    /**
     * Execute one queued block read on the dedicated worker thread.
     *
     * This keeps the expensive device transaction off the caller thread without
     * changing the underlying driver contract yet. Later phases can swap the
     * worker body to true driver-level async submit/poll without touching FAT32.
     *
     * @param request Queued request to execute.
     * @return Nothing.
     */
    void block_io_execute_request(BlockIoRequest* request) {
        SSize result;

        if (request == NULL) {
            return;
        }

        result = request->device->read(request->offset, request->buffer, request->length);
        block_io_complete_request(request, result);
    }

    /**
     * Run the permanent kernel block-I/O worker forever.
     *
     * The worker blocks only when the queue is empty so queued reads remain the
     * single serialization point for storage access until the backend grows more
     * than one in-flight request.
     *
     * @return Never returns.
     */
    [[noreturn]] void service_block_io_worker_entry(void) {
        for (;;) {
            BlockIoRequest* request = block_io_pop_request();

            if (request == NULL) {
                (void)Scheduler::block_current(WaitReason::Event, NULL);
                continue;
            }

            block_io_execute_request(request);
        }
    }

    /**
     * Start the permanent kernel worker that owns queued block reads.
     *
     * Spinning up the worker lazily keeps early boot unchanged until a real
     * storage read needs the service, which keeps this transition low risk.
     *
     * @return StatusOK when the worker is ready to accept requests.
     */
    Status ensure_block_io_worker(void) {
        Process* process = NULL;
        Thread* thread = NULL;
        Status status;

        if ((g_block_io_worker_process != NULL) && (g_block_io_worker_thread != NULL)) {
            return StatusOK;
        }

        status = ProcessManager::create_process("svc-blockio", &process);
        if (status != StatusOK) {
            return status;
        }

        process->header.flags = process->header.flags | ObjectFlags::Permanent | ObjectFlags::Kernel;
        status = ThreadManager::create_thread(
            process,
            "async-blockio",
            reinterpret_cast<VirtAddr>(&service_block_io_worker_entry),
            &thread,
            BlockIoWorkerPriority,
            ThreadDefaultQuantumTicks);
        if (status != StatusOK) {
            (void)ProcessManager::destroy_process(process);
            return status;
        }

        thread->header.flags = thread->header.flags | ObjectFlags::Permanent | ObjectFlags::Kernel;
        status = Scheduler::enqueue(thread);
        if (status != StatusOK) {
            (void)ProcessManager::destroy_process(process);
            return status;
        }

        g_block_io_worker_process = process;
        g_block_io_worker_thread = thread;
        return StatusOK;
    }

} // namespace

/**
 * Queue one block read on the dedicated worker.
 *
 * @param device Device that will satisfy the read.
 * @param offset Byte offset on the device.
 * @param buffer Destination buffer for the read.
 * @param length Byte count to read.
 * @param out_request Receives the queued request on success.
 * @return StatusOK on success, or an error when the request cannot be queued.
 */
Status BlockIoService::submit_read(Device* device, U64 offset, void* buffer, Size length, BlockIoRequest** out_request) {
    BlockIoRequest* request;
    bool interrupts_enabled;
    bool wake_worker = false;
    Status status;

    if (out_request == NULL) {
        return StatusInvalidArgument;
    }
    *out_request = NULL;

    if ((device == NULL) || ((buffer == NULL) && (length != 0U))) {
        return StatusInvalidArgument;
    }

    status = ensure_block_io_worker();
    if (status != StatusOK) {
        return status;
    }

    request = static_cast<BlockIoRequest*>(Heap::alloc(sizeof(BlockIoRequest), alignof(BlockIoRequest)));
    if (request == NULL) {
        return StatusNoMemory;
    }

    memzero(request, sizeof(*request));
    request->device = device;
    request->offset = offset;
    request->buffer = buffer;
    request->length = length;
    request->result = static_cast<SSize>(StatusBusy);

    interrupts_enabled = block_io_lock();
    block_io_link_request(request);
    wake_worker = (g_block_io_worker_thread != NULL)
        && (g_block_io_worker_thread->current_state == ThreadState::Waiting);
    block_io_unlock(interrupts_enabled);

    if (wake_worker) {
        (void)Scheduler::enqueue(g_block_io_worker_thread);
    }

    *out_request = request;
    return StatusOK;
}

/**
 * Wait for one queued request to complete and then release it.
 *
 * @param request Previously queued request to wait on.
 * @param out_result Receives the worker's raw device-read result.
 * @return StatusOK on success, or a wait/argument failure.
 */
Status BlockIoService::wait(BlockIoRequest* request, SSize* out_result) {
    Thread* current_thread;
    bool interrupts_enabled;
    Status status;
    SSize result;

    if ((request == NULL) || (out_result == NULL)) {
        return StatusInvalidArgument;
    }

    for (;;) {
        interrupts_enabled = block_io_lock();
        if (request->completed) {
            result = request->result;
            request->waiting_thread = NULL;
            block_io_unlock(interrupts_enabled);
            *out_result = result;
            block_io_free_request(request);
            return StatusOK;
        }

        current_thread = Scheduler::current();
        if ((current_thread == NULL) || (current_thread->current_state != ThreadState::Running)) {
            block_io_unlock(interrupts_enabled);
            return StatusBusy;
        }
        if ((request->waiting_thread != NULL) && (request->waiting_thread != current_thread)) {
            block_io_unlock(interrupts_enabled);
            return StatusBusy;
        }

        request->waiting_thread = current_thread;
        block_io_unlock(interrupts_enabled);

        /*
         * A completion can race with the narrow window between publishing the
         * waiter pointer and actually blocking. A one-tick timed wait closes
         * that gap without turning the caller back into a long busy loop: the
         * normal fast path still wakes immediately via `Scheduler::enqueue()`,
         * while the timeout path just rechecks the latched completion state.
         */
        status = Scheduler::block_current_for(WaitReason::IO, BlockIoWaitRetryTicks, NULL);

        interrupts_enabled = block_io_lock();
        if (request->waiting_thread == current_thread) {
            request->waiting_thread = NULL;
        }
        block_io_unlock(interrupts_enabled);
        if ((status != StatusOK) && (status != StatusBusy)) {
            return status;
        }
    }
}

/**
 * Report whether one queued request has completed yet.
 *
 * @param request Request to inspect.
 * @param out_completed Receives true when the worker has finished the request.
 * @param out_result Receives the current device-read result snapshot.
 * @return StatusOK on success, or StatusInvalidArgument for bad pointers.
 */
Status BlockIoService::query(BlockIoRequest* request, bool* out_completed, SSize* out_result) {
    bool interrupts_enabled;

    if ((request == NULL) || (out_completed == NULL) || (out_result == NULL)) {
        return StatusInvalidArgument;
    }

    interrupts_enabled = block_io_lock();
    *out_completed = request->completed;
    *out_result = request->result;
    block_io_unlock(interrupts_enabled);
    return StatusOK;
}

/**
 * Perform one synchronous read via the worker-backed block-I/O service.
 *
 * @param device Device that will satisfy the read.
 * @param offset Byte offset on the device.
 * @param buffer Destination buffer for the read.
 * @param length Byte count to read.
 * @return Raw device-read result, or a negative status code on failure.
 */
SSize BlockIoService::read(Device* device, U64 offset, void* buffer, Size length) {
    BlockIoRequest* request = NULL;
    SSize result;
    Status status;

    if ((device == NULL) || ((buffer == NULL) && (length != 0U))) {
        return static_cast<SSize>(StatusInvalidArgument);
    }

    if ((g_block_io_worker_thread != NULL) && (Scheduler::current() == g_block_io_worker_thread)) {
        return device->read(offset, buffer, length);
    }

    status = submit_read(device, offset, buffer, length, &request);
    if (status != StatusOK) {
        return static_cast<SSize>(status);
    }

    status = wait(request, &result);
    if (status != StatusOK) {
        return static_cast<SSize>(status);
    }

    return result;
}