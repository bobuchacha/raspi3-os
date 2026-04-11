/*
 * ldr_reloc.c
 *
 * Apply runtime relocations after section bytes have been copied into memory.
 */
#include "../include/ldr_internal.h"

 /*
  * ldr_reloc_apply
  *
  * Patch relocation targets using runtime module base address.
  *
  * Args:
 *   context - loader context for optional logging.
  *   module - mapped module with relocation table.
  *
  * Returns:
  *   LDR_OK on success, relocation/format error on failure.
  */
LDR_RESULT
ldr_reloc_apply(PLDR_CONTEXT context, PLDR_MODULE module) {
    UInt relocIndex;

    if (!context || !module) {
        return LDR_E_INVALID_ARG;
    }

    /* Walk relocation list and patch each target slot in-place. */
    for (relocIndex = 0; relocIndex < module->relocCount; ++relocIndex) {
        PLDR_RELOC reloc = &module->relocs[relocIndex];
        Address patchAddress = module->baseAddress + reloc->patchRva;
        /* Guard against out-of-range relocation writes into foreign memory. */
        if (reloc->patchRva + sizeof(ULong) > module->imageSize) {
            return LDR_E_FORMAT;
        }

        if (reloc->type != LDR_RELOC_ABS64) {
            return LDR_E_RELOC;
        }

        /* ABS64 semantics: write runtime base plus relocation addend. */
        if (ldr_write_module_u64(module, patchAddress, module->baseAddress + (ULong)reloc->addend) != 0) {
            return LDR_E_VM;
        }
    }

    return LDR_OK;
}
