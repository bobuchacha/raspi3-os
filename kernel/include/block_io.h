#ifndef KERNEL_INCLUDE_BLOCK_IO_H
#define KERNEL_INCLUDE_BLOCK_IO_H

#include "device.h"

#if !defined(__cplusplus)
#error "block_io.h requires C++"
#endif

struct BlockIoRequest;

class BlockIoService final {
public:
    /**
     * Queue one device-backed read on the dedicated kernel block-I/O worker.
     *
     * The first async slice keeps the driver API synchronous while moving the
     * blocking device transaction off the caller thread. Returning an owned
     * request object here gives later phases a stable handle for query and wait
     * operations without forcing FAT32 or VFS to learn the worker internals.
     *
     * @param device Device that will satisfy the read.
     * @param offset Byte offset on the device.
     * @param buffer Destination buffer for the read.
     * @param length Byte count to read.
     * @param out_request Receives the queued request on success.
     * @return StatusOK on success, or an error when the request cannot be queued.
     */
    static Status submit_read(Device* device, U64 offset, void* buffer, Size length, BlockIoRequest** out_request);

    /**
     * Wait until one queued block read completes and release its request object.
     *
     * The service uses `WaitReason::IO` so scheduler state clearly records that
     * the caller is parked on storage completion rather than a generic event.
     * The request object is consumed by this call because the current tree only
     * needs one waiter and the synchronous FAT32 bridge always waits exactly once.
     *
     * @param request Previously queued request to wait on.
     * @param out_result Receives the worker's raw device-read result.
     * @return StatusOK on success, or a wait/argument failure.
     */
    static Status wait(BlockIoRequest* request, SSize* out_result);

    /**
     * Report whether one queued request has completed.
     *
     * This exists so later syscall-facing phases can inspect completion state
     * without blocking, while the first FAT32 integration can stay submit-plus-wait.
     *
     * @param request Request to inspect.
     * @param out_completed Receives true when the worker has finished the request.
     * @param out_result Receives the current device-read result snapshot.
     * @return StatusOK on success, or StatusInvalidArgument for bad pointers.
     */
    static Status query(BlockIoRequest* request, bool* out_completed, SSize* out_result);

    /**
     * Perform one worker-backed device read and wait for completion.
     *
     * This synchronous wrapper keeps FAT32 semantics unchanged for now while the
     * actual storage transaction runs on the dedicated block-I/O worker.
     *
     * @param device Device that will satisfy the read.
     * @param offset Byte offset on the device.
     * @param buffer Destination buffer for the read.
     * @param length Byte count to read.
     * @return Raw device-read result, or a negative status code on failure.
     */
    static SSize read(Device* device, U64 offset, void* buffer, Size length);
};

#endif // KERNEL_INCLUDE_BLOCK_IO_H