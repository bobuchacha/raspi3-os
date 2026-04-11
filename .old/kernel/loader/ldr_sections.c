/*
 * ldr_sections.c
 *
 * Section copy, zero-fill, and final protection tightening for mapped images.
 */
#include "../include/ldr_internal.h"

#include <string.h>

 /*
  * ldr_section_prot_flags
  *
  * Convert section permission flags to VM protection flags.
  *
  * Args:
  *   sectionFlags - LDR_SEC_* section flags.
  *
  * Returns:
  *   OR'ed `LDR_VM_*` protection flags.
  */
static Flags
ldr_section_prot_flags(Flags sectionFlags) {
    Flags vmFlags = 0;
    if (sectionFlags & LDR_SEC_READ) {
        vmFlags |= LDR_VM_READ;
    }
    if (sectionFlags & LDR_SEC_WRITE) {
        vmFlags |= LDR_VM_WRITE;
    }
    if (sectionFlags & LDR_SEC_EXEC) {
        vmFlags |= LDR_VM_EXEC;
    }
    return vmFlags;
}

/*
 * ldr_sections_load
 *
 * Copy initialized section payloads and zero BSS tails.
 *
 * Args:
 *   context - loader context.
 *   module - mapped module with section metadata.
 *   buffer - source file bytes.
 *   size - source file byte length.
 *
 * Returns:
 *   LDR_OK on success, format/vm error on failure.
 */
LDR_RESULT
ldr_sections_load(PLDR_CONTEXT context, PLDR_MODULE module, CONST UByte* buffer, Size size) {
    UInt sectionIndex;

    if (!context || !module || !buffer || size == 0) {
        return LDR_E_INVALID_ARG;
    }

    /* Materialize each section at base + RVA and enforce bounds. */
    for (sectionIndex = 0; sectionIndex < module->sectionCount; ++sectionIndex) {
        PLDR_SECTION section = &module->sections[sectionIndex];
        Address destinationAddress = module->baseAddress + section->rva;
        Flags protectionFlags;

        /* Reject sections that would write outside reserved module image. */
        if (section->rva + section->virtualSize > module->imageSize) {
            return LDR_E_FORMAT;
        }

        /* Copy only the file-backed bytes, then zero-fill the remainder if needed. */
        if (section->fileSize > 0) {
            if (section->fileOffset + section->fileSize > size) {
                return LDR_E_FORMAT;
            }
            if (ldr_write_module_bytes(module, destinationAddress, buffer + section->fileOffset, section->fileSize) != 0) {
                if (context->api->logPrintf) {
                    context->api->logPrintf(0, "ldr_sections_load: write failed for %s section %s at 0x%lX", module->path, section->name, destinationAddress);
                }
                return LDR_E_VM;
            }
        }
        if (section->virtualSize > section->fileSize) {
            if (ldr_zero_module_bytes(module, destinationAddress + section->fileSize, section->virtualSize - section->fileSize) != 0) {
                if (context->api->logPrintf) {
                    context->api->logPrintf(0, "ldr_sections_load: zero failed for %s section %s at 0x%lX", module->path, section->name, destinationAddress + section->fileSize);
                }
                return LDR_E_VM;
            }
        }

        /* Tighten section protection now that copy/zero is done. */
        protectionFlags = ldr_section_prot_flags(section->flags);
        if (module->isKernelModule) {
            protectionFlags |= LDR_VM_KERN;
        }
        else {
            protectionFlags |= LDR_VM_USER;
        }

        if (context->api->vmProtect(destinationAddress, section->virtualSize, protectionFlags) != 0) {
            if (context->api->logPrintf) {
                context->api->logPrintf(0, "ldr_sections_load: protect failed for %s section %s at 0x%lX size=0x%lX flags=0x%lX", module->path, section->name, destinationAddress, section->virtualSize, protectionFlags);
            }
            return LDR_E_VM;
        }
    }

    return LDR_OK;
}
