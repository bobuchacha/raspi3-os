#ifndef KERNEL_INCLUDE_LOADER_H
#define KERNEL_INCLUDE_LOADER_H

#include "types.h"

struct Process;
struct Thread;

typedef enum ImageKind {
    ImageKindKernel = 0,
    ImageKindUser = 1,
    ImageKindModule = 2
} ImageKind;

typedef struct ImageSegment {
    PhysAddr phys_base;
    VirtAddr virt_base;
    Size file_size;
    Size memory_size;
    U64 flags;
} ImageSegment;

typedef struct Image {
    ImageKind kind;
    const void* source;
    Size source_size;
    VirtAddr requested_base;
    VirtAddr entry_point;
    U32 segment_count;
    const ImageSegment* segments;
} Image;

class Loader final {
public:
    static Status init(void);
    static Status validate(const Image* image);
    static Status load(const Image* image, VirtAddr* entry_out);

    /**
     * Load one packed LRD0 executable from VFS into a fresh EL0 process.
     *
     * The current MM layer only supports 2 MiB user mappings, so the loader
     * intentionally targets one self-contained EXE without shared-library
     * imports. That is enough to boot the restored userspace shell while
     * keeping the kernel service-call path small and explicit.
     *
     * @param path VFS path to the packed executable.
     * @param process_name Human-readable process name, or NULL to derive one.
     * @param out_thread Receives the new main thread on success.
     * @return StatusOK on success, or an error if the image cannot be read,
     * validated, mapped, or threaded.
     */
    static Status spawn_user_process(const char* path, const char* process_name, const char* process_arguments, Thread** out_thread);

    /**
     * Return any fixed loader backing slots owned by a user process.
     *
     * The current EL0 loader keeps executable images and user stacks in a
     * small static block pool. When a short-lived process exits, the kernel
     * must hand those slots back explicitly or later spawns will fail even
     * though the process and address space were destroyed successfully.
     *
     * @param process Process whose loader-owned backing slots should be freed.
     * @return StatusOK on success, or StatusInvalidArgument when process is NULL.
     */
    static Status release_user_process_resources(Process* process);
};

#endif // KERNEL_INCLUDE_LOADER_H
