#include "arch/aarch64/exception_debugger.h"

#include "arch.h"
#include "arch/aarch64/exception_disasm.h"
#include "mm.h"
#include "process.h"
#include "scheduler.h"
#include "serial.h"
#include "thread.h"
#include "internal/mm/arch/aarch64/mmu_defs.h"

namespace {

    inline constexpr Size CommandBufferCapacity = 256U;
    inline constexpr Size DisassemblyTextCapacity = 160U;
    inline constexpr U64 EsrExceptionClassShift = 26U;
    inline constexpr U64 EsrExceptionClassMask = 0x3FU;
    inline constexpr U64 EsrDataFaultStatusMask = 0x3FU;
    inline constexpr U64 EsrDataFaultLevelMask = 0x3U;
    inline constexpr U64 EsrDataFaultKindMask = 0x3U;
    inline constexpr U64 EsrBreakpointInstructionClass = 0x3CU;
    inline constexpr Size MemoryBytesPerLine = 16U;
    inline constexpr Size RegisterColumnsPerRow = 3U;
    inline constexpr Size BacktraceMaxFrames = 32U;
    inline constexpr U64 PromptWidth = 5U;
    inline constexpr char PromptText[] = "dbg> ";
    inline constexpr U64 AArch64InstructionSize = 4U;
    inline constexpr U64 ExceptionFrameReservedLowMask = 0xFULL;

    U32 g_debugger_depth = 0U;

    /**
     * Emit one character through the fault-safe UART path.
     *
     * The exception debugger cannot call the scheduler-backed serial helpers,
     * because that would re-enter normal kernel logic while architectural state
     * is half-restored on the exception stack.
     *
     * @param ch Character to send.
     */
    void debug_putc(char ch) {
        board::Serial::putc_raw(ch);
    }

    /**
     * Emit a null-terminated string through the fault-safe UART path.
     *
     * @param text String to send.
     */
    void debug_puts(const char* text) {
        board::Serial::puts_raw(text);
    }

    /**
     * Read one character from the fault-safe UART path.
     *
     * @return One decoded input character.
     */
    char debug_getc(void) {
        return board::Serial::getc_raw();
    }

    /**
     * Report whether one byte is an ASCII space separator.
     *
     * @param ch Character to classify.
     * @return True when the character separates command tokens.
     */
    bool is_space(char ch) {
        return (ch == ' ') || (ch == '\t') || (ch == '\r') || (ch == '\n');
    }

    /**
     * Report whether one byte is an ASCII decimal digit.
     *
     * @param ch Character to classify.
     * @return True when the character is in the decimal digit range.
     */
    bool is_decimal_digit(char ch) {
        return (ch >= '0') && (ch <= '9');
    }

    /**
     * Report whether one byte is an ASCII hexadecimal digit.
     *
     * @param ch Character to classify.
     * @return True when the character is in the hexadecimal digit range.
     */
    bool is_hex_digit(char ch) {
        return is_decimal_digit(ch)
            || ((ch >= 'a') && (ch <= 'f'))
            || ((ch >= 'A') && (ch <= 'F'));
    }

    /**
     * Convert one hexadecimal digit to its numeric value.
     *
     * @param ch Character to convert.
     * @return Digit value in the range [0, 15].
     */
    U8 hex_digit_value(char ch) {
        if (is_decimal_digit(ch)) {
            return static_cast<U8>(ch - '0');
        }
        if ((ch >= 'a') && (ch <= 'f')) {
            return static_cast<U8>(10 + (ch - 'a'));
        }

        return static_cast<U8>(10 + (ch - 'A'));
    }

    /**
     * Compare two null-terminated strings without pulling in a libc helper.
     *
     * @param lhs Left-hand string.
     * @param rhs Right-hand string.
     * @return True when both strings hold the same bytes.
     */
    bool text_equals(const char* lhs, const char* rhs) {
        Size index = 0U;

        if (lhs == rhs) {
            return true;
        }
        if ((lhs == NULL) || (rhs == NULL)) {
            return false;
        }

        while ((lhs[index] != '\0') && (rhs[index] != '\0')) {
            if (lhs[index] != rhs[index]) {
                return false;
            }
            ++index;
        }

        return lhs[index] == rhs[index];
    }

    /**
     * Compare two null-terminated strings case-insensitively in ASCII.
     *
     * The debugger command loop supports a few multi-character verbs such as
     * `bt` and `backtrace`, and case folding here keeps that parser simple
     * without depending on a fuller string library.
     *
     * @param lhs Left-hand string.
     * @param rhs Right-hand string.
     * @return True when both strings hold the same ASCII text.
     */
    bool text_equals_case_insensitive(const char* lhs, const char* rhs) {
        Size index = 0U;

        if (lhs == rhs) {
            return true;
        }
        if ((lhs == NULL) || (rhs == NULL)) {
            return false;
        }

        while ((lhs[index] != '\0') && (rhs[index] != '\0')) {
            char left = lhs[index];
            char right = rhs[index];

            if ((left >= 'A') && (left <= 'Z')) {
                left = static_cast<char>(left - 'A' + 'a');
            }
            if ((right >= 'A') && (right <= 'Z')) {
                right = static_cast<char>(right - 'A' + 'a');
            }
            if (left != right) {
                return false;
            }
            ++index;
        }

        return lhs[index] == rhs[index];
    }

    /**
     * Emit one unsigned integer in decimal form.
     *
     * @param value Number to print.
     */
    void debug_write_unsigned(U64 value) {
        char digits[32];
        Size count = 0U;

        do {
            digits[count++] = static_cast<char>('0' + (value % 10U));
            value /= 10U;
        } while (value != 0U);

        while (count != 0U) {
            debug_putc(digits[--count]);
        }
    }

    /**
     * Emit one fixed-width hexadecimal number.
     *
     * @param value Number to print.
     * @param digit_count Exact number of hexadecimal digits to emit.
     */
    void debug_write_hex(U64 value, U32 digit_count) {
        for (I32 shift = static_cast<I32>((digit_count - 1U) * 4U); shift >= 0; shift -= 4) {
            const U8 nibble = static_cast<U8>((value >> shift) & 0xFULL);
            const char digit = static_cast<char>((nibble < 10U) ? ('0' + nibble) : ('A' + (nibble - 10U)));

            debug_putc(digit);
        }
    }

    /**
     * Emit one pointer-sized hexadecimal address with a `0x` prefix.
     *
     * @param address Address to print.
     */
    void debug_write_address(U64 address) {
        debug_puts("0x");
        debug_write_hex(address, 16U);
    }

    /**
     * Safely add two unsigned values and reject wraparound.
     *
     * @param base Left-hand operand.
     * @param delta Right-hand operand.
     * @param result_out Receives the sum when no wrap occurs.
     * @return True when the addition is representable.
     */
    bool try_add_u64(U64 base, U64 delta, U64* result_out) {
        if (base > (~0ULL - delta)) {
            return false;
        }
        if (result_out != NULL) {
            *result_out = base + delta;
        }
        return true;
    }

    /**
     * Safely subtract two unsigned values and reject underflow.
     *
     * @param base Left-hand operand.
     * @param delta Right-hand operand.
     * @param result_out Receives the difference when no underflow occurs.
     * @return True when the subtraction is representable.
     */
    bool try_subtract_u64(U64 base, U64 delta, U64* result_out) {
        if (delta > base) {
            return false;
        }
        if (result_out != NULL) {
            *result_out = base - delta;
        }
        return true;
    }

    /**
     * Skip ASCII whitespace in a command string.
     *
     * @param cursor Current token pointer.
     * @return Pointer to the first non-space byte.
     */
    const char* skip_spaces(const char* cursor) {
        while ((cursor != NULL) && is_space(*cursor)) {
            ++cursor;
        }

        return cursor;
    }

    /**
     * Determine whether a token is a named register or system register alias.
     *
     * @param token Token text.
     * @param frame Saved exception frame.
     * @param value_out Receives the resolved register value.
     * @return True when the token names a known register.
     */
    bool parse_register_name(const char* token, const AArch64ExceptionFrame* frame, U64* value_out) {
        U64 value = 0U;

        if ((token == NULL) || (frame == NULL)) {
            return false;
        }
        if (((token[0] == 'x') || (token[0] == 'r')) && is_decimal_digit(token[1])) {
            U32 index = static_cast<U32>(token[1] - '0');

            if (is_decimal_digit(token[2])) {
                index = (index * 10U) + static_cast<U32>(token[2] - '0');
                if (token[3] != '\0') {
                    return false;
                }
            }
            else if (token[2] != '\0') {
                return false;
            }

            if (index >= COUNT_OF(frame->general_registers)) {
                return false;
            }

            value = frame->general_registers[index];
        }
        else if (text_equals(token, "sp")) {
            value = frame->stack_pointer;
        }
        else if (text_equals(token, "pc") || text_equals(token, "elr")) {
            value = frame->elr;
        }
        else if (text_equals(token, "spsr")) {
            value = frame->spsr;
        }
        else if (text_equals(token, "esr")) {
            value = frame->esr;
        }
        else if (text_equals(token, "far")) {
            value = frame->far;
        }
        else if (text_equals(token, "sctlr")) {
            value = frame->sctlr;
        }
        else if (text_equals(token, "tcr")) {
            value = frame->tcr;
        }
        else if (text_equals(token, "lr")) {
            value = frame->general_registers[30];
        }
        else if (text_equals(token, "fp")) {
            value = frame->general_registers[29];
        }
        else if (text_equals(token, "usp")) {
            Thread* current = Scheduler::current();

            if (current == NULL) {
                return false;
            }

            value = current->context.user_stack_pointer;
        }
        else {
            return false;
        }

        if (value_out != NULL) {
            *value_out = value;
        }
        return true;
    }

    /**
     * Parse one numeric token in either hexadecimal or decimal form.
     *
     * @param token Token to parse.
     * @param value_out Receives the numeric value.
     * @return True when the token contains a valid number.
     */
    bool parse_number(const char* token, U64* value_out) {
        U64 value = 0U;
        U32 base = 10U;
        Size index = 0U;

        if ((token == NULL) || (token[0] == '\0')) {
            return false;
        }
        if ((token[0] == '0') && ((token[1] == 'x') || (token[1] == 'X'))) {
            base = 16U;
            index = 2U;
            if (token[index] == '\0') {
                return false;
            }
        }

        while (token[index] != '\0') {
            U8 digit;

            if ((base == 10U) && !is_decimal_digit(token[index])) {
                return false;
            }
            if ((base == 16U) && !is_hex_digit(token[index])) {
                return false;
            }

            digit = (base == 16U) ? hex_digit_value(token[index]) : static_cast<U8>(token[index] - '0');
            if (value > ((~0ULL - digit) / base)) {
                return false;
            }

            value = (value * base) + digit;
            ++index;
        }

        if (value_out != NULL) {
            *value_out = value;
        }
        return true;
    }

    /**
     * Parse one expression term as either a register alias or a number.
     *
     * @param token Null-terminated term text.
     * @param frame Saved exception frame.
     * @param value_out Receives the resolved value.
     * @return True when the term is valid.
     */
    bool parse_expression_term(const char* token, const AArch64ExceptionFrame* frame, U64* value_out) {
        return parse_register_name(token, frame, value_out)
            || parse_number(token, value_out);
    }

    /**
     * Parse one bounded expression-term slice.
     *
     * The command parser keeps the full arithmetic expression in one token, so
     * the evaluator copies each `+`/`-` separated term into a fixed buffer
     * before resolving it as either a register alias or a literal number.
     *
     * @param start First byte of the term slice.
     * @param length Number of bytes in the slice.
     * @param frame Saved exception frame.
     * @param value_out Receives the resolved term value.
     * @return True when the slice forms one valid term.
     */
    bool parse_expression_term_slice(const char* start, Size length, const AArch64ExceptionFrame* frame, U64* value_out) {
        char term[32];

        if ((start == NULL) || (length == 0U) || (length >= COUNT_OF(term))) {
            return false;
        }

        for (Size index = 0U; index < length; ++index) {
            term[index] = start[index];
        }
        term[length] = '\0';
        return parse_expression_term(term, frame, value_out);
    }

    /**
     * Parse one debugger address expression.
     *
     * The syntax intentionally stays deterministic and small:
     * `<term> (('+' | '-') <term>)*`, where each term is either a raw number
     * or a named register such as `sp`, `pc`, `lr`, or `x19`.
     *
     * @param token Expression token to parse.
     * @param frame Saved exception frame.
     * @param value_out Receives the computed value.
     * @return True when the expression is valid.
     */
    bool parse_expression(const char* token, const AArch64ExceptionFrame* frame, U64* value_out) {
        U64 accumulated_value = 0U;
        U64 term_value = 0U;
        Size term_start = 0U;
        Size cursor = 0U;
        char pending_operator = '+';

        if ((token == NULL) || (token[0] == '\0')) {
            return false;
        }

        while (true) {
            if ((token[cursor] == '+') || (token[cursor] == '-') || (token[cursor] == '\0')) {
                const Size term_length = cursor - term_start;

                if (!parse_expression_term_slice(&token[term_start], term_length, frame, &term_value)) {
                    return false;
                }

                if (pending_operator == '+') {
                    if (!try_add_u64(accumulated_value, term_value, &accumulated_value)) {
                        return false;
                    }
                }
                else {
                    if (!try_subtract_u64(accumulated_value, term_value, &accumulated_value)) {
                        return false;
                    }
                }

                if (token[cursor] == '\0') {
                    break;
                }

                pending_operator = token[cursor];
                ++cursor;
                term_start = cursor;
                if ((token[cursor] == '+') || (token[cursor] == '-') || (token[cursor] == '\0')) {
                    return false;
                }
                continue;
            }

            ++cursor;
        }

        if (value_out != NULL) {
            *value_out = accumulated_value;
        }
        return true;
    }

    /**
     * Read one command token from the current cursor position.
     *
     * @param cursor Input cursor.
     * @param buffer Output token buffer.
     * @param capacity Output token capacity.
     * @param next_out Receives the cursor positioned after the token.
     * @return True when a token was copied successfully.
     */
    bool next_token(const char* cursor, char* buffer, Size capacity, const char** next_out) {
        Size length = 0U;

        cursor = skip_spaces(cursor);
        if ((cursor == NULL) || (*cursor == '\0') || (buffer == NULL) || (capacity == 0U)) {
            return false;
        }

        while ((cursor[length] != '\0') && !is_space(cursor[length])) {
            if ((length + 1U) >= capacity) {
                return false;
            }
            buffer[length] = cursor[length];
            ++length;
        }
        buffer[length] = '\0';

        if (next_out != NULL) {
            *next_out = &cursor[length];
        }
        return true;
    }

    /**
     * Emit the prompt and re-render the editable command line.
     *
     * The implementation uses a simple redraw strategy instead of terminal
     * deltas because the debugger must stay deterministic and independent of a
     * larger console subsystem.
     *
     * @param buffer Current command contents.
     * @param length Number of active bytes in the buffer.
     * @param cursor Cursor position inside the command.
     */
    void redraw_command_line(const char* buffer, Size length, Size cursor) {
        debug_putc('\r');
        debug_puts(PromptText);
        for (Size index = 0U; index < length; ++index) {
            debug_putc(buffer[index]);
        }
        debug_putc(' ');
        debug_putc('\r');
        debug_puts(PromptText);

        if (cursor != 0U) {
            debug_putc('\x1B');
            debug_putc('[');
            debug_write_unsigned(PromptWidth + cursor);
            debug_putc('C');
        }
    }

    /**
     * Read one editable command line from the serial console.
     *
     * The debugger keeps the familiar left/right/delete handling from the old
     * monitor so operators can tweak long address expressions without retyping
     * them from scratch.
     *
     * @param buffer Destination command buffer.
     * @param capacity Destination buffer capacity.
     */
    void read_command_line(char* buffer, Size capacity) {
        Size length = 0U;
        Size cursor = 0U;

        if ((buffer == NULL) || (capacity == 0U)) {
            return;
        }

        buffer[0] = '\0';
        redraw_command_line(buffer, length, cursor);
        for (;;) {
            char input = debug_getc();

            if (input == '\n') {
                debug_putc('\n');
                buffer[length] = '\0';
                return;
            }

            if ((input == 8) || (input == 127)) {
                if (cursor != 0U) {
                    for (Size index = cursor - 1U; index < length; ++index) {
                        buffer[index] = buffer[index + 1U];
                    }
                    --cursor;
                    --length;
                }
                redraw_command_line(buffer, length, cursor);
                continue;
            }

            if (input == '\x1B') {
                char second = debug_getc();

                if (second == '[') {
                    char third = debug_getc();

                    if (third == 'C') {
                        if (cursor < length) {
                            ++cursor;
                        }
                    }
                    else if (third == 'D') {
                        if (cursor != 0U) {
                            --cursor;
                        }
                    }
                    else if (third == '3') {
                        char fourth = debug_getc();

                        if ((fourth == '~') && (cursor < length)) {
                            for (Size index = cursor; index < length; ++index) {
                                buffer[index] = buffer[index + 1U];
                            }
                            --length;
                        }
                    }
                }
                redraw_command_line(buffer, length, cursor);
                continue;
            }

            if ((input < ' ') || (input >= 127)) {
                continue;
            }
            if ((length + 1U) >= capacity) {
                continue;
            }

            for (Size index = length; index > cursor; --index) {
                buffer[index] = buffer[index - 1U];
            }
            buffer[cursor++] = input;
            ++length;
            buffer[length] = '\0';
            redraw_command_line(buffer, length, cursor);
        }
    }

    /**
     * Read TTBR0_EL1 from the current CPU.
     *
     * @return Raw TTBR0_EL1 value.
     */
    U64 current_ttbr0(void) {
        U64 value;

        __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(value));
        return value;
    }

    /**
     * Read TTBR1_EL1 from the current CPU.
     *
     * @return Raw TTBR1_EL1 value.
     */
    U64 current_ttbr1(void) {
        U64 value;

        __asm__ volatile("mrs %0, ttbr1_el1" : "=r"(value));
        return value;
    }

    /**
     * Read the current frame pointer from the live EL1 call chain.
     *
     * Deliberate debugger sessions continue running on the same kernel stack as
     * the service handler that requested them, so the live frame chain remains
     * available while the debugger prompt is active.
     *
     * @return Current `x29` value.
     */
    U64 current_frame_pointer(void) {
        U64 value;

        __asm__ volatile("mov %0, x29" : "=r"(value));
        return value;
    }

    /**
     * Walk the active translation tables to confirm one virtual address is backed.
     *
     * The debugger validates reads this way so memory inspection failures do not
     * recursively fault inside the debugger itself.
     *
     * @param address Virtual address to validate.
     * @param descriptor_out Receives the final block descriptor on success.
     * @return True when the address is backed by a level-2 block mapping.
     */
    bool resolve_block_descriptor(VirtAddr address, U64* descriptor_out) {
        const U64 root_register = mm::MemoryManager::is_kernel_address(address) ? current_ttbr1() : current_ttbr0();
        const PhysAddr root_phys = root_register & AARCH64_MMU_OUTPUT_ADDRESS_MASK;
        const U64* level0;
        const U64* level1;
        const U64* level2;
        U64 entry;

        if (!mm::MemoryManager::is_user_address(address) && !mm::MemoryManager::is_kernel_address(address)) {
            return false;
        }
        if (root_phys == 0U) {
            return false;
        }

        level0 = reinterpret_cast<const U64*>(mm::MemoryManager::physical_to_kernel(root_phys));
        entry = level0[mm::backend::level_index(address, mm::backend::L0Shift)];
        if ((entry & AARCH64_MMU_DESC_TABLE) != AARCH64_MMU_DESC_TABLE) {
            return false;
        }

        level1 = reinterpret_cast<const U64*>(mm::MemoryManager::physical_to_kernel(entry & AARCH64_MMU_OUTPUT_ADDRESS_MASK));
        entry = level1[mm::backend::level_index(address, mm::backend::L1Shift)];
        if ((entry & AARCH64_MMU_DESC_TABLE) != AARCH64_MMU_DESC_TABLE) {
            return false;
        }

        level2 = reinterpret_cast<const U64*>(mm::MemoryManager::physical_to_kernel(entry & AARCH64_MMU_OUTPUT_ADDRESS_MASK));
        entry = level2[mm::backend::level_index(address, mm::backend::L2Shift)];
        if ((entry & 0x3ULL) != AARCH64_MMU_DESC_BLOCK) {
            return false;
        }

        if (descriptor_out != NULL) {
            *descriptor_out = entry;
        }
        return true;
    }

    /**
     * Check whether a range is readable normal memory.
     *
     * Device mappings are deliberately rejected because peeking MMIO registers
     * from the debugger can acknowledge interrupts or mutate hardware state.
     *
     * @param address First byte of the range.
     * @param length Number of bytes to validate.
     * @return True when the whole range is backed by readable non-device memory.
     */
    bool range_is_safe_to_read(VirtAddr address, Size length) {
        U64 current = address;
        U64 final_address;

        if (length == 0U) {
            return true;
        }
        if (!try_add_u64(address, length - 1U, &final_address)) {
            return false;
        }

        while (current <= final_address) {
            U64 descriptor;
            U64 block_end;

            if (!resolve_block_descriptor(current, &descriptor)) {
                return false;
            }
            if (((descriptor >> 2) & 0x7ULL) == AARCH64_MMU_ATTR_DEVICE) {
                return false;
            }

            block_end = (current & ~(mm::backend::L2BlockSize - 1U)) + mm::backend::L2BlockSize;
            if ((block_end == 0U) || (block_end > final_address)) {
                break;
            }
            current = block_end;
        }

        return true;
    }

    /**
     * Read a byte from validated memory.
     *
     * @param address Address to read.
     * @param value_out Receives the byte value.
     * @return True when the byte was read safely.
     */
    bool read_byte(VirtAddr address, U8* value_out) {
        if ((value_out == NULL) || !range_is_safe_to_read(address, 1U)) {
            return false;
        }

        *value_out = *reinterpret_cast<const volatile U8*>(address);
        return true;
    }

    /**
     * Read one 32-bit instruction from validated memory.
     *
     * @param address Address to read.
     * @param value_out Receives the instruction word.
     * @return True when the instruction word was read safely.
     */
    bool read_instruction_word(VirtAddr address, U32* value_out) {
        if ((value_out == NULL) || !range_is_safe_to_read(address, sizeof(U32))) {
            return false;
        }

        *value_out = *reinterpret_cast<const volatile U32*>(address);
        return true;
    }

    /**
     * Read one 64-bit word from validated memory.
     *
     * The backtrace walker uses this helper so a bad frame pointer stops the
     * walk cleanly instead of recursively faulting inside the debugger.
     *
     * @param address Address to read.
     * @param value_out Receives the loaded word.
     * @return True when the word was read safely.
     */
    bool read_u64(VirtAddr address, U64* value_out) {
        if ((value_out == NULL) || !range_is_safe_to_read(address, sizeof(U64))) {
            return false;
        }

        *value_out = *reinterpret_cast<const volatile U64*>(address);
        return true;
    }

    /**
     * Report whether one saved vector originated from a lower exception level.
     *
     * Lower-EL traps preserve an EL0 frame pointer in `x29`, while same-EL
     * traps preserve an EL1 frame pointer. The backtrace command uses this to
     * pick the right stack bounds.
     *
     * @param vector_id Saved vector slot number.
     * @return True when the frame came from a lower exception level.
     */
    bool is_lower_el_vector(U64 vector_id) {
        return (vector_id >= AArch64ExceptionVectorLowerElA64Sync)
            && (vector_id <= AArch64ExceptionVectorLowerElA32SError);
    }

    /**
     * Report whether the debugger was entered deliberately through `kdebug`.
     *
     * The service layer annotates the saved trap frame so the debugger can
     * present deliberate sessions differently from unexpected faults.
     *
     * @param frame Saved exception frame.
     * @return True when the entry was operator-requested.
     */
    bool is_deliberate_entry(const AArch64ExceptionFrame* frame) {
        return (frame != NULL)
            && ((frame->reserved0 & AARCH64_EXCEPTION_FRAME_FLAG_DELIBERATE_ENTRY) != 0U);
    }

    /**
     * Recover the captured kernel frame pointer for deliberate sessions.
     *
     * The aligned frame pointer is packed into `reserved0` with the low nibble
     * reserved for flag bits.
     *
     * @param frame Saved exception frame.
     * @return Captured kernel frame pointer, or zero when unavailable.
     */
    U64 deliberate_kernel_frame_pointer(const AArch64ExceptionFrame* frame) {
        if (!is_deliberate_entry(frame)) {
            return 0U;
        }

        return frame->reserved0 & ~ExceptionFrameReservedLowMask;
    }

    /**
     * Validate that a candidate frame pointer can hold one frame record.
     *
     * The unwind stays intentionally conservative: it only follows aligned
     * frame records that lie between the current stack pointer and the owning
     * thread's recorded stack top.
     *
     * @param frame_pointer Candidate frame pointer.
     * @param lower_bound Lowest allowed address for the frame chain.
     * @param upper_bound Exclusive upper bound for the frame chain.
     * @return True when the frame record can be read safely.
     */
    bool frame_pointer_is_valid(U64 frame_pointer, U64 lower_bound, U64 upper_bound) {
        U64 frame_record_end;

        if ((frame_pointer == 0U) || ((frame_pointer & 0xFULL) != 0U)) {
            return false;
        }
        if ((upper_bound == 0U) || (upper_bound <= lower_bound)) {
            return false;
        }
        if ((frame_pointer < lower_bound) || (frame_pointer >= upper_bound)) {
            return false;
        }
        if (!try_add_u64(frame_pointer, sizeof(U64) * 2U, &frame_record_end)) {
            return false;
        }

        return (frame_record_end <= upper_bound)
            && range_is_safe_to_read(frame_pointer, sizeof(U64) * 2U);
    }

    /**
     * Validate one live kernel frame pointer before dereferencing it directly.
     *
     * Deliberate debugger sessions run on the same EL1 stack as the service
     * handler that requested them, so reading the current frame chain directly
     * avoids the page-walk restrictions used by generic memory commands.
     *
     * @param frame_pointer Candidate frame pointer.
     * @param upper_bound Exclusive stack-top bound for the current thread.
     * @return True when the frame pointer looks like a valid record on the live stack.
     */
    bool live_frame_pointer_is_valid(U64 frame_pointer, U64 upper_bound) {
        return (frame_pointer != 0U)
            && ((frame_pointer & 0xFULL) == 0U)
            && (frame_pointer < upper_bound)
            && ((upper_bound - frame_pointer) >= (sizeof(U64) * 2U));
    }

    /**
     * Read one live frame record from the current EL1 stack.
     *
     * The deliberate-entry backtrace uses this helper so it can follow the
     * service-side call chain even when the generic page-table walker refuses
     * stack addresses.
     *
     * @param frame_pointer Address of the current frame record.
     * @param next_frame_pointer Receives the caller's frame pointer.
     * @param return_address Receives the saved return address.
     */
    void read_live_frame_record(U64 frame_pointer, U64* next_frame_pointer, U64* return_address) {
        const U64* record = reinterpret_cast<const U64*>(frame_pointer);

        if (next_frame_pointer != NULL) {
            *next_frame_pointer = record[0];
        }
        if (return_address != NULL) {
            *return_address = record[1];
        }
    }

    /**
     * Convert one saved vector identifier into readable text.
     *
     * @param vector_id Saved vector slot number.
     * @return Human-readable vector name.
     */
    const char* vector_name(U64 vector_id) {
        switch (vector_id) {
        case AArch64ExceptionVectorCurrentSp0Sync:
            return "Current EL using SP0 sync";
        case AArch64ExceptionVectorCurrentSp0Irq:
            return "Current EL using SP0 IRQ";
        case AArch64ExceptionVectorCurrentSp0Fiq:
            return "Current EL using SP0 FIQ";
        case AArch64ExceptionVectorCurrentSp0SError:
            return "Current EL using SP0 SError";
        case AArch64ExceptionVectorCurrentSpXSync:
            return "Current EL using SPx sync";
        case AArch64ExceptionVectorCurrentSpXIrq:
            return "Current EL using SPx IRQ";
        case AArch64ExceptionVectorCurrentSpXFiq:
            return "Current EL using SPx FIQ";
        case AArch64ExceptionVectorCurrentSpXSError:
            return "Current EL using SPx SError";
        case AArch64ExceptionVectorLowerElA64Sync:
            return "Lower EL AArch64 sync";
        case AArch64ExceptionVectorLowerElA64Irq:
            return "Lower EL AArch64 IRQ";
        case AArch64ExceptionVectorLowerElA64Fiq:
            return "Lower EL AArch64 FIQ";
        case AArch64ExceptionVectorLowerElA64SError:
            return "Lower EL AArch64 SError";
        case AArch64ExceptionVectorLowerElA32Sync:
            return "Lower EL AArch32 sync";
        case AArch64ExceptionVectorLowerElA32Irq:
            return "Lower EL AArch32 IRQ";
        case AArch64ExceptionVectorLowerElA32Fiq:
            return "Lower EL AArch32 FIQ";
        case AArch64ExceptionVectorLowerElA32SError:
            return "Lower EL AArch32 SError";
        default:
            return "Unknown vector";
        }
    }

    /**
     * Emit a decoded explanation for the ESR exception class and fault subtype.
     *
     * @param frame Saved exception frame.
     */
    void write_exception_reason(const AArch64ExceptionFrame* frame) {
        const U64 exception_class = (frame->esr >> EsrExceptionClassShift) & EsrExceptionClassMask;

        switch (exception_class) {
        case 0x00U:
            debug_puts("Unknown");
            break;
        case 0x01U:
            debug_puts("Trapped WFI/WFE");
            break;
        case 0x0EU:
            debug_puts("Illegal execution state");
            break;
        case 0x15U:
            debug_puts("System call");
            break;
        case 0x20U:
            debug_puts("Instruction abort from lower EL");
            break;
        case 0x21U:
            debug_puts("Instruction abort from same EL");
            break;
        case 0x22U:
            debug_puts("Instruction alignment fault");
            break;
        case 0x24U:
            debug_puts("Data abort from lower EL");
            break;
        case 0x25U:
            debug_puts("Data abort from same EL");
            break;
        case 0x26U:
            debug_puts("Stack alignment fault");
            break;
        case 0x2CU:
            debug_puts("Floating point trap");
            break;
        case 0x30U:
            debug_puts("Breakpoint from lower EL");
            break;
        case 0x31U:
            debug_puts("Breakpoint from same EL");
            break;
        case EsrBreakpointInstructionClass:
            debug_puts("BRK instruction");
            break;
        default:
            debug_puts("Unknown EC=");
            debug_write_hex(exception_class, 2U);
            break;
        }

        if ((exception_class == 0x24U) || (exception_class == 0x25U)) {
            const U64 fault_status = frame->esr & EsrDataFaultStatusMask;

            debug_puts(", ");
            switch ((fault_status >> 2) & EsrDataFaultKindMask) {
            case 0U:
                debug_puts("Address size fault");
                break;
            case 1U:
                debug_puts("Translation fault");
                break;
            case 2U:
                debug_puts("Access flag fault");
                break;
            case 3U:
                debug_puts("Permission fault");
                break;
            }
            debug_puts(" at level ");
            debug_write_unsigned(fault_status & EsrDataFaultLevelMask);
        }
    }

    /**
     * Emit the currently active process and thread, if any.
     *
     * @param process Current process pointer.
     * @param thread Current thread pointer.
     */
    void write_current_execution_owner(const Process* process, const Thread* thread) {
        if (process != NULL) {
            debug_puts("  process: ");
            debug_puts(process->name);
            debug_puts(" (pid=");
            debug_write_unsigned(process->id);
            debug_puts(")\n");
        }
        if (thread != NULL) {
            debug_puts("  thread:  ");
            debug_puts(thread->name);
            debug_puts(" (tid=");
            debug_write_unsigned(thread->id);
            debug_puts(")\n");
        }
    }

    /**
     * Emit the saved architectural register file.
     *
     * @param frame Saved exception frame.
     */
    void dump_registers(const AArch64ExceptionFrame* frame) {
        Thread* current_thread = Scheduler::current();
        Process* current_process = Scheduler::current_process();

        debug_puts("\nRegisters:\n");
        for (U32 index = 0U; index < COUNT_OF(frame->general_registers); ++index) {
            if ((index % RegisterColumnsPerRow) == 0U) {
                debug_puts("  ");
            }

            debug_putc('x');
            if (index < 10U) {
                debug_putc('0');
            }
            debug_write_unsigned(index);
            debug_puts(": ");
            debug_write_hex(frame->general_registers[index], 16U);
            debug_puts("  ");

            if (((index + 1U) % RegisterColumnsPerRow) == 0U) {
                debug_putc('\n');
            }
        }
        if ((COUNT_OF(frame->general_registers) % RegisterColumnsPerRow) != 0U) {
            debug_putc('\n');
        }

        debug_puts("\n  SP:      ");
        debug_write_hex(frame->stack_pointer, 16U);
        debug_puts("\n  ELR_EL1: ");
        debug_write_hex(frame->elr, 16U);
        debug_puts("\n  SPSR_EL1:");
        debug_write_hex(frame->spsr, 16U);
        debug_puts("\n  ESR_EL1: ");
        debug_write_hex(frame->esr, 16U);
        debug_puts("\n  FAR_EL1: ");
        debug_write_hex(frame->far, 16U);
        debug_puts("\n  SCTLR_EL1:");
        debug_write_hex(frame->sctlr, 16U);
        debug_puts("\n  TCR_EL1: ");
        debug_write_hex(frame->tcr, 16U);
        if ((current_thread != NULL) && (current_thread->context.user_stack_pointer != 0U)) {
            debug_puts("\n  SP_EL0:  ");
            debug_write_hex(current_thread->context.user_stack_pointer, 16U);
        }
        debug_putc('\n');

        write_current_execution_owner(current_process, current_thread);
        debug_puts("  vector:  ");
        debug_puts(vector_name(frame->vector_id));
        debug_puts("\n  reason:  ");
        write_exception_reason(frame);
        if (is_deliberate_entry(frame)) {
            debug_puts("\n  source:  deliberate debug-shell request");
        }
        debug_putc('\n');
    }

    /**
     * Emit one formatted memory line from the requested address.
     *
     * @param base_address First byte to display on the line.
     */
    void dump_memory_line(VirtAddr base_address) {
        U8 bytes[MemoryBytesPerLine];
        bool valid[MemoryBytesPerLine];

        for (Size index = 0U; index < COUNT_OF(bytes); ++index) {
            U64 byte_address;

            valid[index] = try_add_u64(base_address, index, &byte_address)
                && read_byte(byte_address, &bytes[index]);
        }

        debug_write_address(base_address);
        debug_puts(":  ");
        for (Size index = 0U; index < COUNT_OF(bytes); ++index) {
            if (valid[index]) {
                debug_write_hex(bytes[index], 2U);
            }
            else {
                debug_puts("??");
            }

            if (((index + 1U) % 4U) == 0U) {
                debug_putc(' ');
            }
            debug_putc(' ');
        }

        for (Size index = 0U; index < COUNT_OF(bytes); ++index) {
            if (!valid[index] || (bytes[index] < 32U) || (bytes[index] >= 127U)) {
                debug_putc('.');
            }
            else {
                debug_putc(static_cast<char>(bytes[index]));
            }
        }
        debug_putc('\n');
    }

    /**
     * Emit a memory dump over the requested address range.
     *
     * @param start_address Inclusive start address.
     * @param end_address Exclusive end address.
     */
    void dump_memory_range(VirtAddr start_address, VirtAddr end_address) {
        U64 current_address = start_address;

        if (end_address <= start_address) {
            if (!try_add_u64(start_address, MemoryBytesPerLine, &end_address)) {
                end_address = ~0ULL;
            }
        }

        debug_putc('\n');
        while (current_address < end_address) {
            dump_memory_line(current_address);
            if (!try_add_u64(current_address, MemoryBytesPerLine, &current_address)) {
                break;
            }
        }
    }

    /**
     * Emit a disassembly listing over the requested address range.
     *
     * @param start_address Inclusive start address.
     * @param end_address Exclusive end address.
     */
    void dump_disassembly_range(VirtAddr start_address, VirtAddr end_address) {
        VirtAddr current_address = start_address & ~(AArch64InstructionSize - 1U);
        char disassembly[DisassemblyTextCapacity];

        if (end_address <= start_address) {
            if (!try_add_u64(current_address, AArch64InstructionSize, &end_address)) {
                end_address = ~0ULL;
            }
        }
        if ((end_address & (AArch64InstructionSize - 1U)) != 0U) {
            U64 aligned_end_address;

            if (!try_add_u64(end_address, AArch64InstructionSize - 1U, &aligned_end_address)) {
                aligned_end_address = ~0ULL;
            }
            end_address = aligned_end_address;
        }
        end_address &= ~(AArch64InstructionSize - 1U);

        debug_putc('\n');
        while (current_address < end_address) {
            U32 instruction_word;

            debug_write_address(current_address);
            debug_puts(":  ");
            if (!read_instruction_word(current_address, &instruction_word)) {
                debug_puts("????????  <unmapped or device memory>\n");
                if (!try_add_u64(current_address, AArch64InstructionSize, &current_address)) {
                    break;
                }
                continue;
            }

            debug_write_hex(instruction_word, 8U);
            debug_puts("  ");
            aarch64_exception_disasm(current_address, disassembly, sizeof(disassembly));
            debug_puts(disassembly);
            debug_putc('\n');
            if (!try_add_u64(current_address, AArch64InstructionSize, &current_address)) {
                break;
            }
        }
    }

    /**
     * Emit a bounded frame-pointer backtrace.
     *
     * Unexpected lower-EL exceptions are most useful when the debugger walks
     * the trapped EL0 frame chain. Deliberate `kdebug break` sessions are more
     * useful when they show the live EL1 call chain that led into the syscall,
     * so the deliberate entry helper captures that kernel frame pointer in the
     * saved exception frame.
     *
     * @param frame Saved exception frame.
     */
    void dump_backtrace(const AArch64ExceptionFrame* frame) {
        Thread* current_thread = Scheduler::current();
        U64 frame_pointer = 0U;
        U64 lower_bound = 0U;
        U64 upper_bound = 0U;
        const char* backtrace_kind = "kernel";
        bool use_live_kernel_chain = false;

        if (frame == NULL) {
            return;
        }

        if (is_deliberate_entry(frame)) {
            frame_pointer = current_frame_pointer();
            lower_bound = 0U;
            upper_bound = (current_thread != NULL) ? current_thread->kernel_stack_top : ~0ULL;
            use_live_kernel_chain = true;

            // Skip the `bt` helper itself, the main debugger loop, and the
            // deliberate-entry wrapper so the first printed frame lands in the
            // service-side call chain that requested the session.
            for (Size skip_index = 0U; skip_index < 3U; ++skip_index) {
                U64 next_frame_pointer;

                if (!live_frame_pointer_is_valid(frame_pointer, upper_bound)) {
                    frame_pointer = deliberate_kernel_frame_pointer(frame);
                    use_live_kernel_chain = false;
                    break;
                }

                read_live_frame_record(frame_pointer, &next_frame_pointer, NULL);
                if ((next_frame_pointer == 0U)
                    || (next_frame_pointer <= frame_pointer)) {
                    frame_pointer = deliberate_kernel_frame_pointer(frame);
                    use_live_kernel_chain = false;
                    break;
                }

                frame_pointer = next_frame_pointer;
            }
        }
        else if (is_lower_el_vector(frame->vector_id)) {
            frame_pointer = frame->general_registers[29];
            lower_bound = (current_thread != NULL) ? current_thread->context.user_stack_pointer : 0U;
            upper_bound = (current_thread != NULL) ? current_thread->user_stack_top : 0U;
            backtrace_kind = "user";
        }
        else {
            frame_pointer = frame->general_registers[29];
            lower_bound = frame->stack_pointer;
            upper_bound = (current_thread != NULL) ? current_thread->kernel_stack_top : 0U;
        }

        debug_putc('\n');
        debug_puts("Backtrace (");
        debug_puts(backtrace_kind);
        debug_puts("):\n");
        debug_puts("  #0  elr=");
        debug_write_address(frame->elr);
        debug_putc('\n');

        if ((!use_live_kernel_chain && !frame_pointer_is_valid(frame_pointer, lower_bound, upper_bound))
            || (use_live_kernel_chain && !live_frame_pointer_is_valid(frame_pointer, upper_bound))) {
            debug_puts("  <no frame-pointer chain>\n");
            return;
        }

        for (Size frame_index = 1U; frame_index < BacktraceMaxFrames; ++frame_index) {
            U64 next_frame_pointer;
            U64 return_address;

            if (use_live_kernel_chain) {
                read_live_frame_record(frame_pointer, &next_frame_pointer, &return_address);
            }
            else {
                U64 lr_address;

                if (!try_add_u64(frame_pointer, sizeof(U64), &lr_address)) {
                    break;
                }
                if (!read_u64(frame_pointer, &next_frame_pointer) || !read_u64(lr_address, &return_address)) {
                    break;
                }
            }

            debug_puts("  #");
            debug_write_unsigned(frame_index);
            debug_puts("  fp=");
            debug_write_address(frame_pointer);
            debug_puts("  lr=");
            debug_write_address(return_address);
            debug_putc('\n');

            if ((next_frame_pointer == 0U) || (return_address == 0U)) {
                break;
            }
            if (next_frame_pointer <= frame_pointer) {
                break;
            }
            if ((!use_live_kernel_chain && !frame_pointer_is_valid(next_frame_pointer, lower_bound, upper_bound))
                || (use_live_kernel_chain && !live_frame_pointer_is_valid(next_frame_pointer, upper_bound))) {
                break;
            }

            frame_pointer = next_frame_pointer;
        }
    }

    /**
     * Parse up to two optional address arguments for `x` and `i` commands.
     *
     * @param command_arguments Command tail after the verb.
     * @param frame Saved exception frame.
     * @param default_address Address used when no argument is supplied.
     * @param start_out Receives the start address.
     * @param end_out Receives the end address.
     * @return True when the argument list is valid.
     */
    bool parse_optional_address_range(
        const char* command_arguments,
        const AArch64ExceptionFrame* frame,
        U64 default_address,
        U64* start_out,
        U64* end_out) {
        char first_token[64];
        char second_token[64];
        const char* cursor = command_arguments;
        U64 start_address = default_address;
        U64 end_address = default_address;

        cursor = skip_spaces(cursor);
        if ((cursor == NULL) || (*cursor == '\0')) {
            if (start_out != NULL) {
                *start_out = start_address;
            }
            if (end_out != NULL) {
                *end_out = end_address;
            }
            return true;
        }

        if (!next_token(cursor, first_token, sizeof(first_token), &cursor) || !parse_expression(first_token, frame, &start_address)) {
            return false;
        }

        cursor = skip_spaces(cursor);
        if ((cursor != NULL) && (*cursor != '\0')) {
            if (!next_token(cursor, second_token, sizeof(second_token), &cursor) || !parse_expression(second_token, frame, &end_address)) {
                return false;
            }
        }
        else {
            end_address = start_address;
        }

        if (start_out != NULL) {
            *start_out = start_address;
        }
        if (end_out != NULL) {
            *end_out = end_address;
        }
        return true;
    }

    /**
     * Emit the debugger help screen.
     */
    void show_help(void) {
        debug_puts(
            "\nCommands:\n"
            "  ? | h           Show this help\n"
            "  r               Dump registers and decoded exception state\n"
            "  bt              Show a bounded frame-pointer backtrace\n"
            "  x [start [end]] Examine memory from start to end\n"
            "  i [start [end]] Disassemble instructions from start to end\n"
            "  c               Continue execution"
        );
        debug_puts("\n\nAddress expressions accept decimal, hexadecimal, and register aliases such as x0, sp, pc, elr, far, lr, and fp.\n");
        debug_puts("Expressions may chain + and - terms, for example: sp+0x100, sp+0x200-16, or pc-4.\n");
    }

    /**
     * Report whether the current trap was caused by an architectural BRK instruction.
     *
     * @param frame Saved exception frame.
     * @return True when continuing should skip the trapped instruction.
     */
    bool should_skip_break_instruction(const AArch64ExceptionFrame* frame) {
        return ((frame->esr >> EsrExceptionClassShift) & EsrExceptionClassMask) == EsrBreakpointInstructionClass;
    }

    /**
     * Print the entry banner when the debugger first opens.
     *
     * @param frame Saved exception frame.
     */
    void show_entry_banner(const AArch64ExceptionFrame* frame) {
        debug_puts("\n[kernel] exception debugger\n");
        debug_puts("  vector: ");
        debug_puts(vector_name(frame->vector_id));
        debug_puts("\n  reason: ");
        write_exception_reason(frame);
        if (is_deliberate_entry(frame)) {
            debug_puts("\n  source: deliberate debug-shell request");
        }
        debug_puts("\n  pc:     ");
        debug_write_address(frame->elr);
        debug_puts("\n  far:    ");
        debug_write_address(frame->far);
        debug_putc('\n');
        write_current_execution_owner(Scheduler::current_process(), Scheduler::current());
        debug_puts("  type '?' for commands\n");
    }

    /**
     * Halt the CPU permanently after a nested debugger failure.
     *
     * @return Never returns.
     */
    [[noreturn]] void halt_in_debugger(void) {
        for (;;) {
            __asm__ volatile("wfi");
        }
    }

} // namespace

extern "C" void aarch64_exception_debugger(AArch64ExceptionFrame* frame) {
    char command_line[CommandBufferCapacity];
    char command[32];

    if (frame == NULL) {
        debug_puts("\n[kernel] exception debugger entered with no frame\n");
        halt_in_debugger();
    }

    arch::Arch::disable_interrupts();
    if (g_debugger_depth != 0U) {
        debug_puts("\n[kernel] nested exception inside debugger\n");
        debug_puts("  vector: ");
        debug_puts(vector_name(frame->vector_id));
        debug_puts("\n  reason: ");
        write_exception_reason(frame);
        debug_puts("\n  elr:    ");
        debug_write_address(frame->elr);
        debug_puts("\n  far:    ");
        debug_write_address(frame->far);
        debug_putc('\n');
        halt_in_debugger();
    }

    ++g_debugger_depth;
    show_entry_banner(frame);

    for (;;) {
        U64 start_address;
        U64 end_address;
        const char* arguments = NULL;

        read_command_line(command_line, sizeof(command_line));
        if (!next_token(command_line, command, sizeof(command), &arguments)) {
            show_help();
            continue;
        }
        if (text_equals_case_insensitive(command, "?")
            || text_equals_case_insensitive(command, "h")
            || text_equals_case_insensitive(command, "help")) {
            show_help();
            continue;
        }
        if (text_equals_case_insensitive(command, "r")
            || text_equals_case_insensitive(command, "regs")
            || text_equals_case_insensitive(command, "registers")) {
            dump_registers(frame);
            continue;
        }
        if (text_equals_case_insensitive(command, "bt")
            || text_equals_case_insensitive(command, "backtrace")) {
            dump_backtrace(frame);
            continue;
        }
        if (text_equals_case_insensitive(command, "c")
            || text_equals_case_insensitive(command, "continue")) {
            if (should_skip_break_instruction(frame)) {
                U64 resumed_address;

                if (try_add_u64(frame->elr, AArch64InstructionSize, &resumed_address)) {
                    frame->elr = resumed_address;
                }
            }
            break;
        }
        if (text_equals_case_insensitive(command, "x")) {
            if (!parse_optional_address_range(arguments, frame, frame->stack_pointer, &start_address, &end_address)) {
                debug_puts("\nerror: invalid memory range\n");
                continue;
            }
            dump_memory_range(start_address, end_address);
            continue;
        }
        if (text_equals_case_insensitive(command, "i")) {
            const U64 default_address = (frame->elr != 0U) ? frame->elr : frame->general_registers[30];

            if (!parse_optional_address_range(arguments, frame, default_address, &start_address, &end_address)) {
                debug_puts("\nerror: invalid disassembly range\n");
                continue;
            }
            dump_disassembly_range(start_address, end_address);
            continue;
        }

        debug_puts("\nerror: unknown command\n");
    }

    --g_debugger_depth;
}

extern "C" void aarch64_exception_debugger_enter_deliberate(AArch64ExceptionFrame* frame) {
    U64 kernel_frame_pointer = 0U;
    U64 caller_frame_pointer = 0U;

    if (frame == NULL) {
        return;
    }

    __asm__ volatile("mov %0, x29" : "=r"(kernel_frame_pointer));

    // Skip the helper's own frame when the record is readable so `bt` starts
    // at the service-side call chain that actually requested the session.
    if (read_u64(kernel_frame_pointer, &caller_frame_pointer)
        && ((caller_frame_pointer & 0xFULL) == 0U)
        && (caller_frame_pointer > kernel_frame_pointer)) {
        kernel_frame_pointer = caller_frame_pointer;
    }

    frame->reserved0 = AARCH64_EXCEPTION_FRAME_FLAG_DELIBERATE_ENTRY;
    if ((kernel_frame_pointer & ExceptionFrameReservedLowMask) == 0U) {
        frame->reserved0 |= kernel_frame_pointer;
    }

    aarch64_exception_debugger(frame);
}
