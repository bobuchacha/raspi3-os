#ifndef KERNEL_INCLUDE_ARCH_AARCH64_EXCEPTION_DISASM_H
#define KERNEL_INCLUDE_ARCH_AARCH64_EXCEPTION_DISASM_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Decode one AArch64 instruction into a human-readable mnemonic string.
     *
     * The debugger keeps this interface narrow so the generated disassembler can
     * stay isolated in one C translation unit with its compatibility formatter.
     *
     * @param address Address of the instruction word to decode.
     * @param buffer Output text buffer that receives the mnemonic and operands.
     * @param capacity Size of the output buffer in bytes.
     * @return Address of the next instruction, which is normally address + 4.
     */
    VirtAddr aarch64_exception_disasm(VirtAddr address, char* buffer, Size capacity);

#ifdef __cplusplus
}
#endif

#endif // KERNEL_INCLUDE_ARCH_AARCH64_EXCEPTION_DISASM_H