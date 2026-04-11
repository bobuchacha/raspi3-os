/*
 * ldr_api.h
 *
 * External kernel integration contract for the loader module.
 *
 * The loader depends on kernel services but does not implement them.
 * A kernel should populate this table and pass it to `ldr_init`.
 */
#ifndef LDR_API_H
#define LDR_API_H

#include "ldr_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct LdrFileHandleStruct
    {
        Address opaque;
    } LDR_FILEHANDLE;

    typedef struct LdrProcessHandleStruct
    {
        Address opaque;
    } LDR_PROCESSHANDLE;

    typedef struct LdrLockHandleStruct
    {
        Address opaque;
    } LDR_LOCKHANDLE;

    typedef LDR_FILEHANDLE *PLDR_FILEHANDLE;
    typedef LDR_PROCESSHANDLE *PLDR_PROCESSHANDLE;
    typedef LDR_LOCKHANDLE *PLDR_LOCKHANDLE;

    /* VM access flags used by loader memory operations. */
    enum
    {
        LDR_VM_READ = 1u << 0,
        LDR_VM_WRITE = 1u << 1,
        LDR_VM_EXEC = 1u << 2,
        LDR_VM_USER = 1u << 3,
        LDR_VM_KERN = 1u << 4
    };

    /*
     * Kernel callback table consumed by loader internals.
     *
     * All callbacks return 0 on success unless otherwise stated.
     */
    typedef struct LdrKernelApiStruct
    {
        /*
         * Reserve a VA range without backing physical pages yet.
         *
         * Args:
         *   preferred_base - requested address (0 means kernel chooses).
         *   size - byte length to reserve.
         *   flags - VM policy flags (`LDR_VM_*`).
         *   out_base - resulting reserved base address.
         *
         * Returns:
         *   0 on success, non-zero error code otherwise.
         */
        Int (*vmReserve) (Address preferredBase, Size size, Flags flags, Address *outBase);

        /* Commit previously reserved memory with desired protection bits. */
        Int (*vmCommit) (Address baseAddress, Size size, Flags flags);

        /* Update memory protection on a committed range. */
        Int (*vmProtect) (Address baseAddress, Size size, Flags flags);

        /* Release a reserved or committed range. */
        Int (*vmRelease) (Address baseAddress, Size size);

        /* Allocate and free kernel heap memory for loader metadata. */
        Pointer (*heapAlloc) (Size size);
        void (*heapFree) (Pointer memory);

        /* Open/read/close file operations used by loader IO path. */
        Int (*vfsOpen) (CONST char *path, Flags flags, PLDR_FILEHANDLE outFile);
        Int (*vfsReadAt) (LDR_FILEHANDLE fileHandle, ULong offset, Pointer buffer, Size size, Size *outRead);
        Int (*vfsSize) (LDR_FILEHANDLE fileHandle, ULong *outSize);
        Int (*vfsClose) (LDR_FILEHANDLE fileHandle);

        /* Process and scheduler hooks used during EXE launch. */
        Int (*procCreate) (CONST char *name, PLDR_PROCESSHANDLE outProcess);
        Int (*procSetEntry) (LDR_PROCESSHANDLE processHandle, Address entryPc, Address stackTop);
        Int (*procAddModule) (LDR_PROCESSHANDLE processHandle, CONST Pointer modulePointer);
        Int (*procStart) (LDR_PROCESSHANDLE processHandle);

        /* Synchronization hooks so loader can serialize module graph updates. */
        Int (*lockCreate) (PLDR_LOCKHANDLE outLock);
        Int (*lockAcquire) (LDR_LOCKHANDLE lockHandle);
        Int (*lockRelease) (LDR_LOCKHANDLE lockHandle);

        /* Logging callback for tracing and diagnostics. */
        void (*logPrintf) (Int level, CONST char *format, ...);
    } LDR_KERNELAPI;

    typedef LDR_KERNELAPI *PLDR_KERNELAPI;
    typedef CONST LDR_KERNELAPI *PCLDR_KERNELAPI;

#ifdef __cplusplus
}
#endif

#endif /* LDR_API_H */
