#include "loader.h"

#include "debug-message.h"
#include "dll_loader.h"
#include "heap.h"
#include "kernel_time.h"
#include "mm.h"
#include "process.h"
#include "resource_manager.h"
#include "scheduler.h"
#include "thread.h"
#include "user_address_space_layout.h"

namespace {

    inline constexpr bool LoaderUserStackPreferBlockMappings = false;
    inline constexpr Size LoaderUserStackBytes = LoaderUserStackPreferBlockMappings ? mm::backend::L2BlockSize : user_address_space::InitialThreadStackSlotBytes;
    inline constexpr VirtAddr UserStackTop = user_address_space::InitialStackBase + LoaderUserStackBytes;

    /**
     * Validate heap integrity at one loader subsystem boundary.
     *
     * The heap rewrite now reports detailed corruption context, so temporary
     * loader checkpoints only need to stop the flow at the first loader phase
     * that sees a broken heap rather than letting the next allocator call hide
     * which subsystem damaged metadata.
     *
     * @param reason Phase label for the validation log.
     * @return StatusOK when the heap is still internally consistent.
     */
    Status loader_heap_checkpoint_status(const char* reason) {
        if (!Heap::debug_validate(reason)) {
            KERROR("[loader] heap checkpoint failed reason=%s\n", reason != NULL ? reason : "<none>");
            return StatusFault;
        }

        return StatusOK;
    }

    /**
     * Emit one compact stack-backing occupancy snapshot.
     *
     * The loader still reserves a small stack pool to avoid fragmentation, but
     * larger GUI workloads may spill onto heap-backed fallback blocks. Logging
     * both sources keeps process-launch scaling failures easy to interpret.
     *
     * @param reason Short phase label for the snapshot.
     * @return Nothing.
     */
    void loader_log_stack_snapshot(const char* reason) {
        KernelResourceStats stats = {};

        KernelResourceManager::get_stats(KernelResourceKind::LoaderStackBacking, &stats);
        KDEBUG(
            KZONE_LOADER,
            "[loader] stack usage reason=%s live=%lu peak=%lu live_bytes=%lu peak_bytes=%lu\n",
            (reason != NULL) ? reason : "<none>",
            static_cast<unsigned long>(stats.live_count),
            static_cast<unsigned long>(stats.peak_count),
            static_cast<unsigned long>(stats.live_bytes),
            static_cast<unsigned long>(stats.peak_bytes));
    }

    /**
     * Return one scheduler-backed millisecond timestamp for spawn profiling.
     *
     * Spawn latency can come from process creation, image loading, mapping, or
     * thread bootstrap. Using the same time source for each boundary keeps the
     * breakdown internally comparable.
     *
     * @return Current uptime in milliseconds.
     */
    U64 loader_now_msec(void) {
        return KernelTime::ticks_to_milliseconds(Scheduler::tick_count());
    }

    void copy_text(char* destination, Size capacity, const char* source, const char* fallback) {
        Size index = 0U;
        const char* active = ((source != NULL) && (source[0] != '\0')) ? source : fallback;

        if ((destination == NULL) || (capacity == 0U)) {
            return;
        }

        while ((active[index] != '\0') && ((index + 1U) < capacity)) {
            destination[index] = active[index];
            ++index;
        }

        destination[index] = '\0';
    }

    const char* path_basename(const char* path) {
        const char* basename = path;

        if (path == NULL) {
            return NULL;
        }

        while (*path != '\0') {
            if ((*path == '/') || (*path == '\\')) {
                basename = path + 1;
            }
            ++path;
        }

        return basename;
    }

    char* duplicate_text(const char* source, const char* fallback) {
        const char* active = ((source != NULL) && (source[0] != '\0')) ? source : fallback;
        Size length = 0U;
        char* copy;

        while (active[length] != '\0') {
            ++length;
        }

        copy = static_cast<char*>(Heap::alloc(length + 1U, alignof(char)));
        if (copy == NULL) {
            return NULL;
        }

        for (Size index = 0U; index < length; ++index) {
            copy[index] = active[index];
        }
        copy[length] = '\0';
        return copy;
    }

    /**
     * Acquire one user-stack backing block.
     *
     * Stack backing now grows through the shared resource table rather than
     * reserving a fixed pool at boot.
     *
     * @return Heap-backed stack block, or NULL when allocation fails.
     */
    U8* allocate_user_block(void) {
        U8* block = static_cast<U8*>(KernelResourceManager::allocate(
            KernelResourceKind::LoaderStackBacking,
            LoaderUserStackBytes,
            mm::PageSize,
            0ULL,
            "user-stack"));

        if (block != NULL) {
            loader_log_stack_snapshot("alloc");
        }
        else {
            loader_log_stack_snapshot("alloc-failed");
        }

        return block;
    }

    /**
     * Release one user-stack backing block back to the loader pool or heap.
     *
     * @param block Stack backing buffer to release.
     * @return Nothing.
     */
    void release_user_block(U8* block) {
        if (block == NULL) {
            return;
        }

        KernelResourceManager::release(block);
        loader_log_stack_snapshot("free");
    }

    Status loader_validate_impl(const Image* image) {
        if (image == NULL) {
            return StatusInvalidArgument;
        }

        return StatusOK;
    }

    Status loader_load_impl(const Image* image, VirtAddr* entry_out) {
        Status status;

        if ((image == NULL) || (entry_out == NULL)) {
            return StatusInvalidArgument;
        }

        status = loader_validate_impl(image);
        if (status != StatusOK) {
            return status;
        }

        *entry_out = image->entry_point;
        return StatusNotSupported;
    }

} // namespace

Status Loader::init(void) {
    Status status = loader_heap_checkpoint_status("loader:init:entry");

    if (status != StatusOK) {
        return status;
    }

    status = DllLoader::init();
    if (status != StatusOK) {
        return status;
    }

    status = KernelResourceManager::init();
    if (status != StatusOK) {
        return status;
    }

    return loader_heap_checkpoint_status("loader:init:success");
}

Status Loader::validate(const Image* image) {
    Status status = loader_heap_checkpoint_status("loader:validate:entry");

    if (status != StatusOK) {
        return status;
    }

    status = loader_validate_impl(image);
    if (status != StatusOK) {
        return status;
    }

    return loader_heap_checkpoint_status("loader:validate:success");
}

Status Loader::load(const Image* image, VirtAddr* entry_out) {
    Status status = loader_heap_checkpoint_status("loader:load:entry");

    if (status != StatusOK) {
        return status;
    }

    status = loader_load_impl(image, entry_out);
    if (status != StatusOK) {
        return status;
    }

    return loader_heap_checkpoint_status("loader:load:success");
}

Status Loader::release_user_process_resources(Process* process) {
    Status status;

    if (process == NULL) {
        return StatusInvalidArgument;
    }

    status = loader_heap_checkpoint_status("loader:release_resources:entry");
    if (status != StatusOK) {
        return status;
    }

    status = DllLoader::release_process_modules(process);
    if (status != StatusOK) {
        return status;
    }

    DllLoader::release_process_image_private_pages(process);

    if (process->loader_image_backing != NULL) {
        DllLoader::release_image_backing(process->loader_image_backing, process->loader_image_bytes);
        process->loader_image_backing = NULL;
        process->loader_image_bytes = 0U;
    }
    if (process->loader_stack_backing != NULL) {
        release_user_block(static_cast<U8*>(process->loader_stack_backing));
        process->loader_stack_backing = NULL;
        process->loader_stack_bytes = 0U;
    }

    return loader_heap_checkpoint_status("loader:release_resources:success");
}

Status Loader::spawn_user_process(const char* path, const char* process_name, const char* process_arguments, Thread** out_thread) {
    Process* process = NULL;
    Thread* thread = NULL;
    U8* image_backing = NULL;
    U8* stack_backing = NULL;
    Size image_bytes = 0U;
    VirtAddr entry_point = 0U;
    Status status;
    char derived_name[ProcessNameCapacity];
    U64 total_start_msec = loader_now_msec();
    U64 stage_start_msec = total_start_msec;
    U64 create_process_msec = 0U;
    U64 stack_ready_msec = 0U;
    U64 load_image_msec = 0U;
    U64 map_msec = 0U;
    U64 thread_create_msec = 0U;

    if ((path == NULL) || (out_thread == NULL)) {
        return StatusInvalidArgument;
    }

    status = loader_heap_checkpoint_status("loader:spawn:entry");
    if (status != StatusOK) {
        return status;
    }

    status = init();
    if (status != StatusOK) {
        return status;
    }

    copy_text(derived_name, sizeof(derived_name), process_name, path_basename(path));
    stage_start_msec = loader_now_msec();
    status = ProcessManager::create_user_process(derived_name, &process);
    create_process_msec = loader_now_msec() - stage_start_msec;
    if (status != StatusOK) {
        return status;
    }

    stage_start_msec = loader_now_msec();
    stack_backing = allocate_user_block();
    stack_ready_msec = loader_now_msec() - stage_start_msec;
    if (stack_backing == NULL) {
        (void)ProcessManager::destroy_process(process);
        (void)loader_heap_checkpoint_status("loader:spawn:stack-failure");
        return StatusNoMemory;
    }
    status = loader_heap_checkpoint_status("loader:spawn:stack-ready");
    if (status != StatusOK) {
        release_user_block(stack_backing);
        (void)ProcessManager::destroy_process(process);
        return status;
    }

    stage_start_msec = loader_now_msec();
    status = DllLoader::load_user_executable(process, path, reinterpret_cast<void**>(&image_backing), &image_bytes, &entry_point);
    load_image_msec = loader_now_msec() - stage_start_msec;
    if (status != StatusOK) {
        release_user_block(stack_backing);
        (void)ProcessManager::destroy_process(process);
        (void)loader_heap_checkpoint_status("loader:spawn:image-failure");
        return status;
    }
    status = loader_heap_checkpoint_status("loader:spawn:image-ready");
    if (status != StatusOK) {
        release_user_block(stack_backing);
        DllLoader::release_image_backing(image_backing, image_bytes);
        (void)ProcessManager::destroy_process(process);
        return status;
    }

    process->launch_arguments = duplicate_text(process_arguments, "");
    if (process->launch_arguments == NULL) {
        release_user_block(stack_backing);
        DllLoader::release_image_backing(image_backing, image_bytes);
        (void)ProcessManager::destroy_process(process);
        (void)loader_heap_checkpoint_status("loader:spawn:arguments-failure");
        return StatusNoMemory;
    }
    process->loader_image_backing = image_backing;
    process->loader_image_bytes = image_bytes;
    process->loader_stack_backing = stack_backing;
    process->loader_stack_bytes = LoaderUserStackBytes;
    process->user_stack_slot_bytes = LoaderUserStackBytes;
    process->user_stack_slot_bitmap = 1U;
    copy_text(process->image_path, sizeof(process->image_path), path, "");
    status = loader_heap_checkpoint_status("loader:spawn:arguments-ready");
    if (status != StatusOK) {
        (void)ProcessManager::destroy_process(process);
        return status;
    }

    {
        const VmMapping stack_mapping = {
            user_address_space::InitialStackBase,
            mm::MemoryManager::kernel_to_physical(reinterpret_cast<VirtAddr>(stack_backing)),
            LoaderUserStackBytes,
            PagePresent | PageWritable | PageUser,
        };

        stage_start_msec = loader_now_msec();
        status = DllLoader::map_user_executable(process, image_backing);
        if (status == StatusOK) {
            status = mm::MemoryManager::map(&process->process_address_space, &stack_mapping);
        }
        map_msec = loader_now_msec() - stage_start_msec;
    }
    if (status != StatusOK) {
        (void)ProcessManager::destroy_process(process);
        (void)loader_heap_checkpoint_status("loader:spawn:map-failure");
        return status;
    }
    status = loader_heap_checkpoint_status("loader:spawn:mapped");
    if (status != StatusOK) {
        (void)ProcessManager::destroy_process(process);
        return status;
    }

    stage_start_msec = loader_now_msec();
    status = ThreadManager::create_user_thread(process, "main", entry_point, UserStackTop, &thread);
    thread_create_msec = loader_now_msec() - stage_start_msec;
    if (status != StatusOK) {
        (void)ProcessManager::destroy_process(process);
        (void)loader_heap_checkpoint_status("loader:spawn:thread-failure");
        return status;
    }

    *out_thread = thread;
    KRETAIL(
        "[loader-prof] spawn path=%s create_process_ms=%llu stack_ms=%llu load_image_ms=%llu map_ms=%llu thread_ms=%llu total_ms=%llu\n",
        path,
        static_cast<unsigned long long>(create_process_msec),
        static_cast<unsigned long long>(stack_ready_msec),
        static_cast<unsigned long long>(load_image_msec),
        static_cast<unsigned long long>(map_msec),
        static_cast<unsigned long long>(thread_create_msec),
        static_cast<unsigned long long>(loader_now_msec() - total_start_msec));
    return loader_heap_checkpoint_status("loader:spawn:success");
}
