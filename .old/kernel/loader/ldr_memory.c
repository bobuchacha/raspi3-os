/*
 * ldr_memory.c
 *
 * Helpers that let loader stages write into either kernel or user module memory.
 */
#include "../include/arch/cortex-a53/mmu.h"
#include "../include/memory.h"
#include "../include/ldr_internal.h"

 /*
  * Translate one module-relative address into a kernel-mapped address that the
  * loader can safely write through, regardless of whether the module lives in
  * kernel or user space.
  */
static Int
ldr_translate_module_address(PCLDR_MODULE module, Address address, Address* outKernelAddress) {
    Task* task;
    MmuWalkResult walk;

    if (!module || !outKernelAddress) {
        return -1;
    }

    if (module->isKernelModule) {
        *outKernelAddress = address;
        return 0;
    }

    task = (Task*)module->ownerProcess.opaque;
    if (!task) {
        return -1;
    }
    if (process_walk_page(task, address & MM_PAGE_MASK, &walk) != 0) {
        return -1;
    }

    *outKernelAddress = (walk.phys_addr + VA_START) + (address & (PAGE_SIZE - 1));
    return 0;
}

/* Write an arbitrary byte range into mapped module memory, even across pages. */
Int
ldr_write_module_bytes(PLDR_MODULE module, Address destinationAddress, CONST void* sourceBuffer, Size byteCount) {
    CONST UByte* sourceCursor = (CONST UByte*)sourceBuffer;
    Address currentAddress = destinationAddress;
    Size remainingBytes = byteCount;

    if (!module || (!sourceCursor && byteCount != 0)) {
        return -1;
    }

    while (remainingBytes > 0) {
        Address kernelAddress;
        Size pageRemainder;
        Size chunkSize;

        if (ldr_translate_module_address(module, currentAddress, &kernelAddress) != 0) {
            return -1;
        }

        pageRemainder = PAGE_SIZE - (currentAddress & (PAGE_SIZE - 1));
        chunkSize = remainingBytes < pageRemainder ? remainingBytes : pageRemainder;
        memmove((void*)kernelAddress, sourceCursor, (unsigned int)chunkSize);

        currentAddress += chunkSize;
        sourceCursor += chunkSize;
        remainingBytes -= chunkSize;
    }

    return 0;
}

/* Zero-fill an arbitrary byte range inside mapped module memory. */
Int
ldr_zero_module_bytes(PLDR_MODULE module, Address destinationAddress, Size byteCount) {
    Address currentAddress = destinationAddress;
    Size remainingBytes = byteCount;

    if (!module) {
        return -1;
    }

    while (remainingBytes > 0) {
        Address kernelAddress;
        Size pageRemainder;
        Size chunkSize;

        if (ldr_translate_module_address(module, currentAddress, &kernelAddress) != 0) {
            return -1;
        }

        pageRemainder = PAGE_SIZE - (currentAddress & (PAGE_SIZE - 1));
        chunkSize = remainingBytes < pageRemainder ? remainingBytes : pageRemainder;
        memset((void*)kernelAddress, 0, (size_t)chunkSize);

        currentAddress += chunkSize;
        remainingBytes -= chunkSize;
    }

    return 0;
}

/* Write one 64-bit value into mapped module memory. */
Int
ldr_write_module_u64(PLDR_MODULE module, Address destinationAddress, ULong value) {
    return ldr_write_module_bytes(module, destinationAddress, &value, sizeof(value));
}