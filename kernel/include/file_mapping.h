#pragma once
#include "types.h"

struct Process;

typedef U64 FileMappingId;

typedef enum class FileMappingPrivilege {
    ReadOnly,
    ReadWrite
} FileMappingPrivilege;


namespace FileMapping {
    /**
     * Create a file mapping object for a given path.
     *
     * @param filePath The path to the file to be mapped.
     * @param size The size of the mapping in bytes.
     * @param handleOut Receives the new file-mapping handle on success.
     * @return StatusOK on success, or a kernel status code on failure.
     */
    Status CreateFileMapping(const char* filePath, unsigned long size, FileMappingId* handleOut);

    /**
     * Open an existing file mapping object for a given path.
     *
     * @param filePath The path to the file whose mapping is to be opened.
     * @param size The size of the mapping in bytes.
     * @param handleOut Receives the file-mapping handle on success.
     * @return StatusOK on success, or a kernel status code on failure.
     */
    Status OpenFileMapping(const char* filePath, unsigned long size, FileMappingId* handleOut);

    /**
     * Close one file mapping handle.
     *
     * @param mappingHandle Handle to release.
     * @return StatusOK on success, or a kernel status code on failure.
     */
    Status CloseFileMapping(FileMappingId mappingHandle);

    /**
     * Grow one existing file mapping to a larger logical size.
     *
     * Resize is grow-only so existing attachments keep a stable base address
     * while the kernel appends new pages at the end of the mapping.
     *
     * @param mappingHandle Handle to the file mapping object.
     * @param size New logical size in bytes.
     * @return StatusOK on success, or a kernel status code on failure.
     */
    Status ResizeFileMapping(FileMappingId mappingHandle, unsigned long size);

    /**
     * Map a view of a file mapping into the address space of the calling process.
     *
     * @param process Destination process that should receive the mapping.
     * @param mappingHandle The handle to the file mapping object.
     * @param offset The offset in the file where the mapping should start.
     * @param size The size of the view to be mapped in bytes.
     * @param addressOut Receives the mapped view address on success.
     * @return StatusOK on success, or a kernel status code on failure.
     */
    Status MapViewOfFile(Process* process, FileMappingId mappingHandle, unsigned long offset, unsigned long size, void** addressOut);

    /**
     * Unmap a previously mapped view of a file mapping from the address space of the calling process.
     *
     * @param process Destination process that owns the mapping.
     * @param baseAddress A pointer to the base address of the mapped view to be unmapped.
     * @return StatusOK on success, or a kernel status code on failure.
     */
    Status UnmapViewOfFile(Process* process, void* baseAddress);

    /**
     * Release every file-mapping view still owned by one process.
     *
     * Process teardown needs an explicit sweep here because file mappings keep
     * their own attachment registry and their physical backing should be
     * released promptly once the last dead process view disappears.
     *
     * @param process Process whose file-mapping views should be destroyed.
     * @return StatusOK on success, or a kernel status code on failure.
     */
    Status release_process_mappings(Process* process);
};