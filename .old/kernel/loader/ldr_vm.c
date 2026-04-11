/*
 * ldr_vm.c
 *
 * Virtual-memory reservation and commit helpers for mapped loader images.
 */
#include "../include/ldr_internal.h"

 /*
  * ldr_vm_map_image
  *
  * Reserve and commit memory for one module image.
  *
  * Args:
  *   context - loader context with VM callbacks.
  *   module - parsed module with image size and preferred base.
  *
  * Returns:
  *   LDR_OK on success; VM-related error codes otherwise.
  */
LDR_RESULT
ldr_vm_map_image(PLDR_CONTEXT context, PLDR_MODULE module) {
    Address baseAddress = 0;
    Flags vmFlags;

    if (!context || !module || module->imageSize == 0) {
        return LDR_E_INVALID_ARG;
    }

    /* Pick user/kernel protection domain from the resolved load policy. */
    vmFlags = LDR_VM_READ | LDR_VM_WRITE;
    if (module->isKernelModule) {
        vmFlags |= LDR_VM_KERN;
    }
    else {
        vmFlags |= LDR_VM_USER;
    }

    /* Reserve full image range first to keep section RVAs contiguous. */
    if (context->api->vmReserve(module->preferredBase, module->imageSize, vmFlags, &baseAddress) != 0) {
        return LDR_E_VM;
    }

    /* Commit once at RW, then section loader/protection phase can tighten permissions. */
    if (context->api->vmCommit(baseAddress, module->imageSize, vmFlags) != 0) {
        (void)context->api->vmRelease(baseAddress, module->imageSize);
        return LDR_E_VM;
    }

    module->baseAddress = baseAddress;
    return LDR_OK;
}
