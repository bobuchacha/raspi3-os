#ifndef KERNEL_INCLUDE_DLL_LOADER_H
#define KERNEL_INCLUDE_DLL_LOADER_H

#include "types.h"

#if !defined(__cplusplus)
#error "dll_loader.h requires C++"
#endif

struct Process;
struct LoadedModule;

inline constexpr U32 DllLoaderSnapshotSectionCapacity = 16U;
inline constexpr U32 DllLoaderSnapshotSectionNameCapacity = 12U;

struct DllLoaderSectionSnapshot {
    unsigned long start_address;
    unsigned long end_address;
    unsigned long flags;
    char name[DllLoaderSnapshotSectionNameCapacity];
};

/*
 * The DLL loader owns PE-style image parsing, module residency, import binding,
 * and export lookup for EL0 images. The public `Loader` class keeps process
 * creation, while this class manages shared-library state inside one process.
 */
class DllLoader final {
public:
    static Status init(void);
    static bool is_module_handle(Process* process, VirtAddr module_handle);

    static Status load_user_executable(
        Process* process,
        const char* path,
        void** image_backing_out,
        Size* image_bytes_out,
        VirtAddr* entry_out);

    static Status map_user_executable(
        Process* process,
        const void* image_backing);

    static Status load_library(
        Process* process,
        const char* module_path,
        VirtAddr* module_handle_out,
        bool* needs_process_attach_out);

    static Status get_proc_address(
        Process* process,
        VirtAddr module_handle,
        const char* module_path,
        const char* export_name,
        VirtAddr* export_address_out);

    static Status free_library(
        Process* process,
        VirtAddr module_handle,
        const char* module_path,
        VirtAddr* action_handle_out);

    static Status snapshot_process_modules(
        Process* process,
        void* modules,
        Size capacity,
        Size* count_out);

    /*
     * Copy one contiguous window of module snapshot records for a process.
     *
     * The task-module syscall now streams records out in small chunks so it no
     * longer needs a large fixed array on the kernel stack. The loader keeps a
     * precomputed registry snapshot in each live module record, and this API
     * copies a caller-selected slice of that registry without mutating loader
     * state.
     *
     * @param process Target process whose modules should be enumerated.
     * @param start_index Zero-based first module index to copy.
     * @param modules Destination buffer for copied records, or NULL to only count.
     * @param capacity Number of destination record slots available.
     * @param copied_count_out Receives the number of copied records.
     * @param total_count_out Receives the total module count for the process.
     * @return StatusOK on success, or StatusNoSpace when more records remain.
     */
    static Status snapshot_process_modules_window(
        Process* process,
        Size start_index,
        void* modules,
        Size capacity,
        Size* copied_count_out,
        Size* total_count_out);

    /*
     * Release one process-owned executable patch-page list.
     *
     * Shared executable backings now stay canonical and immutable while each
     * process keeps its own cloned pages for import slots that must point at
     * that process's chosen DLL bases. Process teardown therefore needs one
     * explicit cleanup hook for those patch pages before the shared backing
     * reference is dropped.
     *
     * @param process Process whose executable patch pages should be released.
     * @return Nothing.
     */
    static void release_process_image_private_pages(Process* process);

    static void release_image_backing(void* backing, Size backing_bytes);
    static Status release_process_modules(Process* process);
    static Status destroy_module_object(LoadedModule* module);
};

#endif /* KERNEL_INCLUDE_DLL_LOADER_H */