/**
 * process.h
 *
 * this file contains the descriptions of a process
 *
 * keep this file clearly annotated
 */

#pragma once

#include "address-space.h"
#include "object.h"
#include "types.h"

#if !defined(__cplusplus)
#error "process.h requires C++"
#endif

struct Thread;
struct HandleTables;

typedef U64 Pid;
inline constexpr U32 ProcessNameCapacity = 32U;

enum class ProcessState : U32 {
    Created = 0,
    Running,
    Exiting,
    Terminated,
};

struct Process {
    ObjectHeader header;
    Pid id;
    char name[ProcessNameCapacity];
    char* launch_arguments;
    ProcessState current_state;
    AddressSpace process_address_space;
    // User processes keep direct references to the heap-backed image and EL0
    // stack blocks that the loader mapped into their address space.
    void* loader_image_backing;
    Size loader_image_bytes;
    void* loader_stack_backing;
    Size loader_stack_bytes;
    // Preserve the parent PID separately from the live parent pointer so child
    // wait and task-info queries remain stable even after pointer teardown.
    Pid parent_process_id;
    Process* parent;
    Thread* main_thread;
    Thread* thread_list_head;
    Thread* thread_list_tail;
    Size thread_count;
    HandleTables* handles;
    U64 exit_code;
    Process* next;
    Process* prev;
};

class ProcessManager final {
public:
    static Status init(void);
    static Status create_process(const char* name, Process** out_process);
    /**
     * Create a user-mode process with its own TTBR0-backed address space.
     *
     * Kernel threads continue to use `create_process()` so they inherit the
     * bootstrap identity map, while EL0 programs get an isolated user view that
     * starts empty and is populated explicitly by the loader.
     *
     * @param name Human-readable process name used for diagnostics.
     * @param out_process Receives the created process on success.
     * @return StatusOK on success, or an error if the slot or address space
     * cannot be allocated.
     */
    static Status create_user_process(const char* name, Process** out_process);
    static Status destroy_process(Process* process);
    static Status terminate_process(Pid id, U64 exit_code);
    /**
     * Reap processes that already requested exit and are waiting for a safe
     * teardown point.
     *
     * Processes in this kernel are peers after creation. The stored parent PID
     * is launcher metadata only, so any later safe kernel path may finalize an
     * `Exiting` process once it is no longer the current owner of execution.
     *
     * @return StatusOK after the scan completes.
     */
    static Status reap_exiting(void);
    /**
     * Consume one stored exit result for a direct child process.
     *
     * Child processes are destroyed eagerly once teardown completes, so a
     * parent waiting afterward needs a stable result cache instead of a live
     * process object.
     *
     * @param id Child PID whose exit code should be collected.
     * @param parent_id Direct parent PID expected to own that child.
     * @param exit_code_out Receives the stored exit code on success.
     * @return StatusOK when one matching exit result was consumed, or
     * StatusNotFound when no result is currently available.
     */
    static Status consume_exit_status(Pid id, Pid parent_id, U64* exit_code_out);
    static Process* find_process(Pid id);
    static Process* find_process(const char* name);
    static Status attach_thread(Process* process, Thread* thread);
    static Status detach_thread(Process* process, Thread* thread);
    static Size process_count(void);
};