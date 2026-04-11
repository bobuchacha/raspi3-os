#ifndef KERNEL_INCLUDE_DLL_LOADER_H
#define KERNEL_INCLUDE_DLL_LOADER_H

#include "types.h"

#if !defined(__cplusplus)
#error "dll_loader.h requires C++"
#endif

struct Process;
struct LoadedModule;

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

    static void release_image_backing(void* backing, Size backing_bytes);
    static Status release_process_modules(Process* process);
    static Status destroy_module_object(LoadedModule* module);
};

#endif /* KERNEL_INCLUDE_DLL_LOADER_H */