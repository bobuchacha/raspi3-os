#include "arch.h"
#include "device.h"
#include "debug-message.h"
#include "framebuffer.h"
#include "filesystem.h"
#include "filesystem/fat32.h"
#include "heap.h"
#include "mm/physical.h"
#include "kernel_event_broker.h"
#include "loader.h"
#include "mm.h"
#include "object.h"
#include "platform.h"
#include "process.h"
#include "scheduler.h"
#include "serial.h"
#include "shared_memory.h"
#include "thread.h"
#include "vfs.h"

extern "C" [[noreturn]] void scheduler_thread_exit_current(void);

namespace {

    inline constexpr Size SchedulerStressThreadCount = 1000U;
    inline constexpr Size VfsAliasScanLimit = 8U;
    inline constexpr U8 KernelImageMagic[8] = { 'K', 'E', 'R', 'N', 'E', 'L', 0U, 0U };
    inline constexpr char LongFileNameSamplePath[] = "C:\\Long File Name.txt";
    inline constexpr char LongFileNameSampleText[] = "lfn sample\n";
    inline constexpr char WritableSamplePath[] = "C:\\Writable Sample.txt";
    inline constexpr char WritableSampleOriginalText[] = "initial writable sample\n";
    inline constexpr char WritableSampleUpdatedText[] = "kernel write ok\n";
    inline constexpr char CreateRemoveDirectoryPath[] = "C:\\Create Remove Smoke";
    inline constexpr char CreateRemoveFileName[] = "Created Through VFS.txt";
    inline constexpr char CreateRemoveFilePath[] = "C:\\Create Remove Smoke\\Created Through VFS.txt";
    inline constexpr char CreateRemoveFileText[] = "fat32 create remove ok\n";
    inline constexpr char UserCorePath[] = "C:\\bin\\core.exe";
    inline constexpr char UserShellPath[] = "C:\\bin\\shell.exe";
    inline constexpr char SharedMemorySmokeName[] = "kernel-smoke-shared";
    inline constexpr char KernelEventSmokeProcessName[] = "kevent-smoke";
    inline constexpr char ProcessSmokeKernelName[] = "proc-smoke";
    inline constexpr char ProcessSmokeUserName[] = "user-smoke";
    inline constexpr char ThreadSmokeProcessName[] = "thread-smoke";
    inline constexpr char ThreadSmokeName[] = "thread-smk";
    inline constexpr VirtAddr VirtualMemorySmokeAddress = (12U * mm::backend::L2BlockSize) + mm::PageSize;
    inline constexpr Size SchedulerPrioritySmokeThreadCount = 4U;
    inline constexpr Size HeapStressBlockCount = 16U;
    inline constexpr Size HeapStressRoundCount = 8U;
    inline constexpr Size HeapAlignmentStressCount = 5U;

    /*
     * Keep the full boot smoke matrix in one visible table so bring-up can
     * disable noisy or expensive checks without deleting the underlying test
     * code.
     */
    typedef struct KernelBootTestConfig {
        bool heap;
        bool process;
        bool memory_map;
        bool physical_memory;
        bool virtual_memory;
        bool kernel_input_path;
        bool kernel_output_path;
        bool kernel_event;
        bool shared_memory;
        bool threads;
        bool scheduler;
        bool platform;
        bool vfs_namespace;
    } KernelBootTestConfig;

    inline constexpr KernelBootTestConfig g_kernel_boot_test_config = {
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
    };

    /*
     * Keep debug tuning next to the boot smoke matrix so one edit can narrow
     * runtime traces by zone or by source file during early investigation.
     */
    inline constexpr bool KernelBootDebugReplaceZoneMask = false;
    inline constexpr U32 KernelBootDebugZoneMask = 0U;
    inline constexpr U32 KernelBootDebugEnableZoneMask = 0U;
    inline constexpr U32 KernelBootDebugDisableZoneMask = 0U;
    inline constexpr Size KernelBootDebugEnabledFileCount = 0U;
    inline constexpr Size KernelBootDebugDisabledFileCount = 3U;
    const char* const g_kernel_boot_debug_enabled_files[] = { "" };
    const char* const g_kernel_boot_debug_disabled_files[] = {
        "heap.cpp",
        "mm.cpp",
        "gui_service.cpp",
    };

    bool g_scheduler_smoke_complete;
    bool g_scheduler_stress_failed;
    Size g_scheduler_stress_first_pass_count;
    Size g_scheduler_stress_completed_count;
    Thread* g_scheduler_stress_threads[SchedulerStressThreadCount];
    U8 g_scheduler_stress_run_counts[SchedulerStressThreadCount];
    bool g_scheduler_priority_failed;
    Size g_scheduler_priority_event_count;
    Size g_scheduler_priority_completed_count;
    Thread* g_scheduler_priority_threads[SchedulerPrioritySmokeThreadCount];
    U8 g_scheduler_priority_phases[SchedulerPrioritySmokeThreadCount];
    char g_scheduler_priority_events[SchedulerPrioritySmokeThreadCount + 2U];
    bool g_list_sdcard_process_running;

    typedef struct VfsSmokeEnumerationContext {
        bool saw_kernel;
        bool saw_long_name_sample;
        bool saw_writable_sample;
    } VfsSmokeEnumerationContext;

    typedef struct SdcardListContext {
        Size depth;
    } SdcardListContext;

    typedef struct VfsNameCheckContext {
        const char* expected_name;
        bool found;
    } VfsNameCheckContext;

    Status run_scheduler_stress_test(void);
    Status run_scheduler_priority_smoke_test(void);

    void scheduler_smoke_entry(void) {
        g_scheduler_smoke_complete = true;
    }

    void thread_smoke_entry(void) {
        scheduler_thread_exit_current();
    }

    int ascii_upper(int ch) {
        if ((ch >= 'a') && (ch <= 'z')) {
            return ch - ('a' - 'A');
        }

        return ch;
    }

    bool same_text_case_insensitive(const char* lhs, const char* rhs) {
        if (lhs == rhs) {
            return true;
        }
        if ((lhs == NULL) || (rhs == NULL)) {
            return false;
        }

        while ((*lhs != '\0') && (*rhs != '\0')) {
            if (ascii_upper(*lhs) != ascii_upper(*rhs)) {
                return false;
            }
            ++lhs;
            ++rhs;
        }

        return *lhs == *rhs;
    }

    bool is_space(char ch) {
        return (ch == ' ') || (ch == '\t');
    }

    bool next_token(char** cursor, char* token, Size capacity) {
        Size length = 0U;

        if ((cursor == NULL) || (*cursor == NULL) || (token == NULL) || (capacity == 0U)) {
            return false;
        }

        while (is_space(**cursor)) {
            ++(*cursor);
        }
        if (**cursor == '\0') {
            token[0] = '\0';
            return false;
        }

        while ((**cursor != '\0') && !is_space(**cursor)) {
            if ((length + 1U) >= capacity) {
                return false;
            }
            token[length++] = **cursor;
            ++(*cursor);
        }

        token[length] = '\0';
        return true;
    }

    void serial_write_char(char ch) {
        board::Serial::putc(ch);
    }

    void serial_write_text(const char* text) {
        board::Serial::puts(text);
    }

    void serial_write_decimal(U64 value) {
        char digits[21];
        Size count = 0U;

        do {
            digits[count++] = static_cast<char>('0' + (value % 10U));
            value /= 10U;
        } while (value != 0U);

        while (count != 0U) {
            serial_write_char(digits[--count]);
        }
    }

    void halt_with_status(const char* step, Status status) {
        serial_write_text("[kernel] ");
        serial_write_text(step);
        serial_write_text(" failed status=");
        if (status < 0) {
            serial_write_char('-');
            serial_write_decimal(static_cast<U64>(-static_cast<I64>(status)));
        }
        else {
            serial_write_decimal(static_cast<U64>(status));
        }
        serial_write_char('\n');
        arch::Arch::halt();
    }

    void log_status(const char* step, Status status) {
        serial_write_text("[kernel] ");
        serial_write_text(step);
        serial_write_text(" status=");
        if (status < 0) {
            serial_write_char('-');
            serial_write_decimal(static_cast<U64>(-static_cast<I64>(status)));
        }
        else {
            serial_write_decimal(static_cast<U64>(status));
        }
        serial_write_char('\n');
    }

    void serial_write_indent(Size depth) {
        for (Size index = 0U; index < depth; ++index) {
            serial_write_text("  ");
        }
    }

    /**
     * Fill one heap allocation with a deterministic byte pattern.
     *
     * The heap stress suite uses repeatable payload contents so realloc, free,
     * and fragmentation phases can verify whether the allocator preserved the
     * caller-visible bytes exactly as expected.
     *
     * @param pointer Allocation to fill.
     * @param length Logical payload length to initialize.
     * @param seed Per-allocation seed that keeps neighboring buffers distinct.
     */
    void heap_stress_fill(void* pointer, Size length, U8 seed) {
        U8* bytes = static_cast<U8*>(pointer);

        if (bytes == NULL) {
            return;
        }

        for (Size index = 0U; index < length; ++index) {
            bytes[index] = static_cast<U8>(seed + static_cast<U8>(index));
        }
    }

    /**
     * Verify that one heap allocation still contains its deterministic pattern.
     *
     * Data preservation is the quickest functional check for split, coalesce,
     * and realloc-copy bugs because it tells the boot smoke exactly which byte
     * changed and at what phase the allocator stopped preserving payload state.
     *
     * @param pointer Allocation to inspect.
     * @param length Logical payload length to check.
     * @param seed Expected seed previously written by `heap_stress_fill`.
     * @param mismatch_index_out Optional index of the first mismatched byte.
     * @return True when every byte matches the expected pattern.
     */
    bool heap_stress_verify(const void* pointer, Size length, U8 seed, Size* mismatch_index_out) {
        const U8* bytes = static_cast<const U8*>(pointer);

        if (mismatch_index_out != NULL) {
            *mismatch_index_out = 0U;
        }
        if (bytes == NULL) {
            return false;
        }

        for (Size index = 0U; index < length; ++index) {
            if (bytes[index] != static_cast<U8>(seed + static_cast<U8>(index))) {
                if (mismatch_index_out != NULL) {
                    *mismatch_index_out = index;
                }
                return false;
            }
        }

        return true;
    }

    /**
     * Release a temporary heap working set used by the boot-time stress suite.
     *
     * The cleanup helper clears both the pointer table and the logical-size
     * table so failure paths can call it repeatedly without double-freeing.
     *
     * @param blocks Allocation tracking table.
     * @param logical_sizes Logical payload sizes for the tracked allocations.
     * @param count Number of tracked entries.
     */
    void heap_stress_cleanup(void** blocks, Size* logical_sizes, Size count) {
        if ((blocks == NULL) || (logical_sizes == NULL)) {
            return;
        }

        for (Size index = 0U; index < count; ++index) {
            if (blocks[index] != NULL) {
                Heap::free(blocks[index]);
                blocks[index] = NULL;
            }
            logical_sizes[index] = 0U;
        }
    }

    /**
     * Run one heap metadata checkpoint and convert any failure into `StatusFault`.
     *
     * The new allocator validator already prints a detailed corruption dump, so
     * the boot smoke only needs to attach the current phase label and stop on
     * the first inconsistent heap walk.
     *
     * @param reason Phase label passed through to `Heap::debug_validate`.
     * @return StatusOK when the heap remains internally consistent.
     */
    Status heap_stress_checkpoint(const char* reason) {
        if (!Heap::debug_validate(reason)) {
            KERROR("[heap-stress] checkpoint failed reason=%s\n", reason != NULL ? reason : "<none>");
            return StatusFault;
        }

        return StatusOK;
    }

    /**
     * Exercise the slab bins with repeated allocate, fragment, refill, and free cycles.
     *
     * The small-bin portion of the new heap should survive heavy churn without
     * losing payload contents or leaving leaked slab pages behind after cleanup.
     *
     * @return StatusOK when all churn rounds complete successfully.
     */
    Status run_heap_small_bin_churn_test(void) {
        static const Size InitialSizes[HeapStressBlockCount] = {
            24U, 32U, 48U, 64U, 72U, 96U, 112U, 128U,
            144U, 160U, 192U, 224U, 256U, 320U, 384U, 480U,
        };
        static const Size RefillSizes[HeapStressBlockCount] = {
            16U, 40U, 56U, 80U, 88U, 104U, 120U, 136U,
            152U, 168U, 208U, 240U, 272U, 336U, 400U, 496U,
        };
        void* blocks[HeapStressBlockCount];
        Size logical_sizes[HeapStressBlockCount];
        U8 seeds[HeapStressBlockCount];

        memzero(blocks, sizeof(blocks));
        memzero(logical_sizes, sizeof(logical_sizes));
        memzero(seeds, sizeof(seeds));

        for (Size round = 0U; round < HeapStressRoundCount; ++round) {
            for (Size index = 0U; index < HeapStressBlockCount; ++index) {
                const Size requested_size = InitialSizes[(index + round) % COUNT_OF(InitialSizes)];

                blocks[index] = Heap::alloc(requested_size, 0U);
                if (blocks[index] == NULL) {
                    heap_stress_cleanup(blocks, logical_sizes, COUNT_OF(blocks));
                    return StatusNoMemory;
                }

                logical_sizes[index] = requested_size;
                seeds[index] = static_cast<U8>(0x20U + static_cast<U8>(round * 3U) + static_cast<U8>(index));
                heap_stress_fill(blocks[index], logical_sizes[index], seeds[index]);
            }

            if (heap_stress_checkpoint("heap-stress:small:initial") != StatusOK) {
                heap_stress_cleanup(blocks, logical_sizes, COUNT_OF(blocks));
                return StatusFault;
            }

            for (Size index = 1U; index < HeapStressBlockCount; index += 2U) {
                Heap::free(blocks[index]);
                blocks[index] = NULL;
                logical_sizes[index] = 0U;
            }

            if (heap_stress_checkpoint("heap-stress:small:fragmented") != StatusOK) {
                heap_stress_cleanup(blocks, logical_sizes, COUNT_OF(blocks));
                return StatusFault;
            }

            for (Size index = 0U; index < HeapStressBlockCount; index += 2U) {
                Size mismatch_index = 0U;

                if (!heap_stress_verify(blocks[index], logical_sizes[index], seeds[index], &mismatch_index)) {
                    KERROR("[heap-stress] small verify failed round=%llu slot=%llu offset=%llu\n",
                        static_cast<unsigned long long>(round),
                        static_cast<unsigned long long>(index),
                        static_cast<unsigned long long>(mismatch_index));
                    heap_stress_cleanup(blocks, logical_sizes, COUNT_OF(blocks));
                    return StatusFault;
                }
            }

            for (Size index = 1U; index < HeapStressBlockCount; index += 2U) {
                const Size refill_size = RefillSizes[(index + round) % COUNT_OF(RefillSizes)];

                blocks[index] = Heap::alloc(refill_size, 0U);
                if (blocks[index] == NULL) {
                    heap_stress_cleanup(blocks, logical_sizes, COUNT_OF(blocks));
                    return StatusNoMemory;
                }

                logical_sizes[index] = refill_size;
                seeds[index] = static_cast<U8>(0x70U + static_cast<U8>(round * 5U) + static_cast<U8>(index));
                heap_stress_fill(blocks[index], logical_sizes[index], seeds[index]);
            }

            if (heap_stress_checkpoint("heap-stress:small:refilled") != StatusOK) {
                heap_stress_cleanup(blocks, logical_sizes, COUNT_OF(blocks));
                return StatusFault;
            }

            for (Size index = 0U; index < HeapStressBlockCount; ++index) {
                Size mismatch_index = 0U;

                if (!heap_stress_verify(blocks[index], logical_sizes[index], seeds[index], &mismatch_index)) {
                    KERROR("[heap-stress] small final verify failed round=%llu slot=%llu offset=%llu\n",
                        static_cast<unsigned long long>(round),
                        static_cast<unsigned long long>(index),
                        static_cast<unsigned long long>(mismatch_index));
                    heap_stress_cleanup(blocks, logical_sizes, COUNT_OF(blocks));
                    return StatusFault;
                }
            }

            heap_stress_cleanup(blocks, logical_sizes, COUNT_OF(blocks));
            if (heap_stress_checkpoint("heap-stress:small:cleanup") != StatusOK) {
                return StatusFault;
            }
        }

        return StatusOK;
    }

    /**
     * Exercise the general allocator with explicit alignment-sensitive requests.
     *
     * The heap rewrite must preserve page and L2 alignment without reviving the
     * older tail-placement corruption bug, so the stress suite verifies the
     * returned addresses and payload integrity for each aligned request.
     *
     * @return StatusOK when all aligned requests succeed and validate.
     */
    Status run_heap_alignment_stress_test(void) {
        typedef struct HeapAlignmentCase {
            Size size;
            Size alignment;
            U8 seed;
            const char* label;
        } HeapAlignmentCase;

        static const HeapAlignmentCase Cases[HeapAlignmentStressCount] = {
            { 33U, 64U, 0x11U, "align64" },
            { 111U, 256U, 0x22U, "align256" },
            { mm::PageSize / 2U, mm::PageSize, 0x33U, "page-half" },
            { mm::PageSize + 137U, mm::PageSize, 0x44U, "page-plus" },
            { mm::PageSize, mm::backend::L2BlockSize, 0x55U, "l2-aligned" },
        };
        void* blocks[COUNT_OF(Cases)] = {};

        for (Size index = 0U; index < COUNT_OF(Cases); ++index) {
            blocks[index] = Heap::alloc(Cases[index].size, Cases[index].alignment);
            if (blocks[index] == NULL) {
                for (Size cleanup_index = 0U; cleanup_index < COUNT_OF(blocks); ++cleanup_index) {
                    if (blocks[cleanup_index] != NULL) {
                        Heap::free(blocks[cleanup_index]);
                    }
                }
                return StatusNoMemory;
            }
            if ((reinterpret_cast<Uptr>(blocks[index]) & static_cast<Uptr>(Cases[index].alignment - 1U)) != 0U) {
                KERROR("[heap-stress] alignment failed case=%s ptr=%llx alignment=%llu\n",
                    Cases[index].label,
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(blocks[index])),
                    static_cast<unsigned long long>(Cases[index].alignment));
                for (Size cleanup_index = 0U; cleanup_index < COUNT_OF(blocks); ++cleanup_index) {
                    if (blocks[cleanup_index] != NULL) {
                        Heap::free(blocks[cleanup_index]);
                    }
                }
                return StatusFault;
            }

            heap_stress_fill(blocks[index], Cases[index].size, Cases[index].seed);
        }

        if (heap_stress_checkpoint("heap-stress:aligned:allocated") != StatusOK) {
            for (Size index = 0U; index < COUNT_OF(blocks); ++index) {
                if (blocks[index] != NULL) {
                    Heap::free(blocks[index]);
                }
            }
            return StatusFault;
        }

        for (Size index = 0U; index < COUNT_OF(Cases); ++index) {
            Size mismatch_index = 0U;

            if (!heap_stress_verify(blocks[index], Cases[index].size, Cases[index].seed, &mismatch_index)) {
                KERROR("[heap-stress] alignment verify failed case=%s offset=%llu\n",
                    Cases[index].label,
                    static_cast<unsigned long long>(mismatch_index));
                for (Size cleanup_index = 0U; cleanup_index < COUNT_OF(blocks); ++cleanup_index) {
                    if (blocks[cleanup_index] != NULL) {
                        Heap::free(blocks[cleanup_index]);
                    }
                }
                return StatusFault;
            }
        }

        for (Size index = COUNT_OF(blocks); index != 0U; --index) {
            Heap::free(blocks[index - 1U]);
            blocks[index - 1U] = NULL;
        }

        return heap_stress_checkpoint("heap-stress:aligned:cleanup");
    }

    /**
     * Exercise realloc growth and shrink paths across slab and general allocations.
     *
     * The new allocator has distinct small-bin and general paths, so the stress
     * suite intentionally grows one allocation across that boundary and then
     * shrinks it again while checking that the preserved prefix bytes survive.
     *
     * @return StatusOK when realloc preserved all expected payload bytes.
     */
    Status run_heap_realloc_stress_test(void) {
        U8* block = static_cast<U8*>(Heap::alloc(48U, 0U));
        U8* general_block = static_cast<U8*>(Heap::alloc(1024U, 64U));

        if ((block == NULL) || (general_block == NULL)) {
            if (block != NULL) {
                Heap::free(block);
            }
            if (general_block != NULL) {
                Heap::free(general_block);
            }
            return StatusNoMemory;
        }

        heap_stress_fill(block, 48U, 0x61U);
        heap_stress_fill(general_block, 1024U, 0x92U);

        {
            U8* grown = static_cast<U8*>(Heap::realloc(block, 96U));
            Size mismatch_index = 0U;

            if ((grown == NULL) || !heap_stress_verify(grown, 48U, 0x61U, &mismatch_index)) {
                KERROR("[heap-stress] realloc 48->96 verify failed offset=%llu\n",
                    static_cast<unsigned long long>(mismatch_index));
                if (grown != NULL) {
                    block = grown;
                }
                Heap::free(block);
                Heap::free(general_block);
                return (grown == NULL) ? StatusNoMemory : StatusFault;
            }
            block = grown;
            heap_stress_fill(block, 96U, 0x6AU);
        }

        {
            U8* grown = static_cast<U8*>(Heap::realloc(block, 640U));
            Size mismatch_index = 0U;

            if ((grown == NULL) || !heap_stress_verify(grown, 96U, 0x6AU, &mismatch_index)) {
                KERROR("[heap-stress] realloc 96->640 verify failed offset=%llu\n",
                    static_cast<unsigned long long>(mismatch_index));
                if (grown != NULL) {
                    block = grown;
                }
                Heap::free(block);
                Heap::free(general_block);
                return (grown == NULL) ? StatusNoMemory : StatusFault;
            }
            block = grown;
            heap_stress_fill(block, 640U, 0x74U);
        }

        {
            U8* shrunk = static_cast<U8*>(Heap::realloc(block, 72U));
            Size mismatch_index = 0U;

            if ((shrunk == NULL) || !heap_stress_verify(shrunk, 72U, 0x74U, &mismatch_index)) {
                KERROR("[heap-stress] realloc 640->72 verify failed offset=%llu\n",
                    static_cast<unsigned long long>(mismatch_index));
                if (shrunk != NULL) {
                    block = shrunk;
                }
                Heap::free(block);
                Heap::free(general_block);
                return (shrunk == NULL) ? StatusNoMemory : StatusFault;
            }
            block = shrunk;
        }

        {
            U8* grown = static_cast<U8*>(Heap::realloc(general_block, 4096U));
            Size mismatch_index = 0U;

            if ((grown == NULL) || !heap_stress_verify(grown, 1024U, 0x92U, &mismatch_index)) {
                KERROR("[heap-stress] realloc 1024->4096 verify failed offset=%llu\n",
                    static_cast<unsigned long long>(mismatch_index));
                if (grown != NULL) {
                    general_block = grown;
                }
                Heap::free(block);
                Heap::free(general_block);
                return (grown == NULL) ? StatusNoMemory : StatusFault;
            }
            general_block = grown;
            heap_stress_fill(general_block, 4096U, 0xA3U);
        }

        {
            U8* shrunk = static_cast<U8*>(Heap::realloc(general_block, 256U));
            Size mismatch_index = 0U;

            if ((shrunk == NULL) || !heap_stress_verify(shrunk, 256U, 0xA3U, &mismatch_index)) {
                KERROR("[heap-stress] realloc 4096->256 verify failed offset=%llu\n",
                    static_cast<unsigned long long>(mismatch_index));
                if (shrunk != NULL) {
                    general_block = shrunk;
                }
                Heap::free(block);
                Heap::free(general_block);
                return (shrunk == NULL) ? StatusNoMemory : StatusFault;
            }
            general_block = shrunk;
        }

        if (heap_stress_checkpoint("heap-stress:realloc:steady") != StatusOK) {
            Heap::free(block);
            Heap::free(general_block);
            return StatusFault;
        }

        Heap::free(block);
        Heap::free(general_block);
        return heap_stress_checkpoint("heap-stress:realloc:cleanup");
    }

    /**
     * Verify that deliberate tail-canary damage is detected and recoverable.
     *
     * The test snapshots the allocator-owned canary bytes, flips one byte past
     * the requested payload, expects validation to fail, restores the canary,
     * and then expects validation to succeed again before the block is freed.
     *
     * @return StatusOK when the allocator detects and then recovers from the probe.
     */
    Status run_heap_corruption_detection_test(void) {
        U8* probe = static_cast<U8*>(Heap::alloc(80U, 0U));
        U8 saved_tail[sizeof(U64)];

        if (probe == NULL) {
            return StatusNoMemory;
        }

        heap_stress_fill(probe, 80U, 0xC4U);
        memcopy(saved_tail, probe + 80U, sizeof(saved_tail));
        probe[80U] ^= 0x5AU;
        if (Heap::debug_validate("heap-stress:corruption:expected-failure")) {
            KERROR("[heap-stress] corruption probe did not trip validation\n");
            memcopy(probe + 80U, saved_tail, sizeof(saved_tail));
            Heap::free(probe);
            return StatusFault;
        }

        memcopy(probe + 80U, saved_tail, sizeof(saved_tail));
        if (heap_stress_checkpoint("heap-stress:corruption:restored") != StatusOK) {
            Heap::free(probe);
            return StatusFault;
        }

        Heap::free(probe);
        return heap_stress_checkpoint("heap-stress:corruption:cleanup");
    }

    /**
     * Run the full boot-time heap stress suite and confirm the heap returns to baseline.
     *
     * The suite covers small-bin churn, large-alignment requests, realloc growth
     * and shrink behavior, and explicit corruption detection while asserting that
     * the heap counters return to the same baseline after cleanup.
     *
     * @return StatusOK when every heap stress phase completes successfully.
     */
    Status run_heap_stress_test(void) {
        HeapStats before = {};
        HeapStats after = {};
        Status status;

        Heap::get_stats(&before);
        status = heap_stress_checkpoint("heap-stress:entry");
        if (status != StatusOK) {
            return status;
        }

        status = run_heap_small_bin_churn_test();
        if (status != StatusOK) {
            return status;
        }
        status = run_heap_alignment_stress_test();
        if (status != StatusOK) {
            return status;
        }
        status = run_heap_realloc_stress_test();
        if (status != StatusOK) {
            return status;
        }
        status = run_heap_corruption_detection_test();
        if (status != StatusOK) {
            return status;
        }

        status = heap_stress_checkpoint("heap-stress:exit");
        if (status != StatusOK) {
            return status;
        }

        Heap::get_stats(&after);
        if ((before.total_bytes != after.total_bytes) || (before.used_bytes != after.used_bytes) || (before.free_bytes != after.free_bytes)) {
            KERROR("[heap-stress] stats mismatch before=(%llu,%llu,%llu) after=(%llu,%llu,%llu)\n",
                static_cast<unsigned long long>(before.total_bytes),
                static_cast<unsigned long long>(before.used_bytes),
                static_cast<unsigned long long>(before.free_bytes),
                static_cast<unsigned long long>(after.total_bytes),
                static_cast<unsigned long long>(after.used_bytes),
                static_cast<unsigned long long>(after.free_bytes));
            return StatusFault;
        }

        return StatusOK;
    }

    Status start_userspace_process(const char* path, const char* process_name, const char* ready_message) {
        Thread* user_thread = NULL;
        Status status = Loader::spawn_user_process(path, process_name, "", &user_thread);

        if (status != StatusOK) {
            return status;
        }

        status = Scheduler::enqueue(user_thread);
        if (status != StatusOK) {
            return status;
        }

        board::Serial::puts(ready_message);
        scheduler_thread_exit_current();
    }

    Status start_userspace_core(void) {
        return start_userspace_process(UserCorePath, "core", "[kernel] userspace core ready\n");
    }

    Status start_userspace_shell(void) {
        return start_userspace_process(UserShellPath, "shell", "[kernel] userspace shell ready\n");
    }

    /**
     * Apply the boot-time debug switchboard.
     *
     * Zone masks still provide the coarse CE-style routing, while the file
     * lists let bring-up isolate one subsystem without recompiling its local
     * trace macros or muting every other caller in the same zone.
     *
     * @return StatusOK when the requested debug filters were installed.
     */
    Status configure_kernel_debug_boot_settings(void) {
        kernel_debug_clear_file_filters();

        if (KernelBootDebugReplaceZoneMask) {
            kernel_debug_set_zone_mask(KernelBootDebugZoneMask);
        }

        for (U32 zone_bit = 0U; zone_bit < 16U; ++zone_bit) {
            const U32 zone_mask = (1U << zone_bit);

            if ((KernelBootDebugEnableZoneMask & zone_mask) != 0U) {
                kernel_debug_enable_zone(zone_bit);
            }
            if ((KernelBootDebugDisableZoneMask & zone_mask) != 0U) {
                kernel_debug_disable_zone(zone_bit);
            }
        }

        for (Size index = 0U; index < KernelBootDebugEnabledFileCount; ++index) {
            Status status = kernel_debug_add_enabled_file(g_kernel_boot_debug_enabled_files[index]);

            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }
        }

        for (Size index = 0U; index < KernelBootDebugDisabledFileCount; ++index) {
            Status status = kernel_debug_add_disabled_file(g_kernel_boot_debug_disabled_files[index]);

            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }
        }

        return StatusOK;
    }

    /**
     * Execute one optional boot smoke test and emit a consistent status line.
     *
     * Keeping the enable check in one helper stops `kernel_main()` from
     * turning into a long run of copy-pasted `if` blocks while still making the
     * active step names obvious in the serial boot log.
     *
     * @param step_name Human-readable test name used in logs and failures.
     * @param enabled Whether the test should execute in this boot.
     * @param test_entry No-argument test function to invoke when enabled.
     * @param ready_message Success message printed after the test passes.
     * @return StatusOK on success or skip, otherwise the failing test status.
     */
    Status run_optional_boot_test(const char* step_name, bool enabled, Status(*test_entry)(void), const char* ready_message) {
        Status status;

        if ((step_name == NULL) || (test_entry == NULL) || (ready_message == NULL)) {
            return StatusInvalidArgument;
        }
        if (!enabled) {
            serial_write_text("[kernel] ");
            serial_write_text(step_name);
            serial_write_text(" skipped\n");
            return StatusOK;
        }

        status = test_entry();
        if (status != StatusOK) {
            return status;
        }

        serial_write_text(ready_message);
        return StatusOK;
    }

    /**
     * Validate process creation and teardown against both kernel and EL0
     * process types.
     *
     * The loader and scheduler rely on the process registry returning to a
     * stable baseline after short-lived helper processes come and go.
     *
     * @return StatusOK when both process variants can be created and removed.
     */
    Status run_process_manager_smoke_test(void) {
        Process* kernel_process = NULL;
        Process* user_process = NULL;
        const Size baseline_processes = ProcessManager::process_count();
        Status status = ProcessManager::create_process(ProcessSmokeKernelName, &kernel_process);

        if (status != StatusOK) {
            return status;
        }
        if ((kernel_process == NULL) || (ProcessManager::process_count() != (baseline_processes + 1U))) {
            (void)ProcessManager::destroy_process(kernel_process);
            return StatusFault;
        }

        status = ProcessManager::create_user_process(ProcessSmokeUserName, &user_process);
        if (status != StatusOK) {
            (void)ProcessManager::destroy_process(kernel_process);
            return status;
        }
        if ((user_process == NULL) || (ProcessManager::process_count() != (baseline_processes + 2U))) {
            (void)ProcessManager::destroy_process(user_process);
            (void)ProcessManager::destroy_process(kernel_process);
            return StatusFault;
        }

        status = ProcessManager::destroy_process(user_process);
        if (status != StatusOK) {
            (void)ProcessManager::destroy_process(kernel_process);
            return status;
        }

        status = ProcessManager::destroy_process(kernel_process);
        if (status != StatusOK) {
            return status;
        }
        if (ProcessManager::process_count() != baseline_processes) {
            return StatusFault;
        }

        return StatusOK;
    }

    /**
     * Validate thread object allocation and teardown without depending on one
     * full scheduler run.
     *
     * This isolates the thread registry, stack allocation, and parent-process
     * linkage from the later scheduler suite so failures surface at the right
     * ownership layer.
     *
     * @return StatusOK when a temporary thread can be created and destroyed.
     */
    Status run_thread_manager_smoke_test(void) {
        Process* process = NULL;
        Thread* thread = NULL;
        const Size baseline_threads = ThreadManager::thread_count();
        const Size baseline_processes = ProcessManager::process_count();
        Status status = ProcessManager::create_process(ThreadSmokeProcessName, &process);

        if (status != StatusOK) {
            return status;
        }

        status = ThreadManager::create_thread(process, ThreadSmokeName, reinterpret_cast<VirtAddr>(&thread_smoke_entry), &thread);
        if (status != StatusOK) {
            (void)ProcessManager::destroy_process(process);
            return status;
        }
        if ((thread == NULL) || (ThreadManager::thread_count() != (baseline_threads + 1U))) {
            (void)ThreadManager::destroy_thread(thread);
            (void)ProcessManager::destroy_process(process);
            return StatusFault;
        }

        status = ThreadManager::destroy_thread(thread);
        if (status != StatusOK) {
            return status;
        }
        if ((ThreadManager::thread_count() != baseline_threads) || (ProcessManager::process_count() != baseline_processes)) {
            return StatusFault;
        }

        return StatusOK;
    }

    /**
     * Validate the user address-space metadata that later loaders and heaps
     * rely on for per-process mappings.
     *
     * This test focuses on the translation-table ownership model rather than a
     * specific mapping operation, which keeps the resource-accounting failure at
     * the address-space layer.
     *
     * @return StatusOK when the temporary address space is fully created and destroyed.
     */
    Status run_memory_map_smoke_test(void) {
        AddressSpace address_space = {};
        Status status = mm::MemoryManager::create_user_address_space(&address_space);

        if (status != StatusOK) {
            return status;
        }
        if ((address_space.page_table_root == 0U)
            || (address_space.translation_table_l0 == 0U)
            || (address_space.translation_table_l1 == 0U)
            || (address_space.translation_table_l2 == 0U)
            || (address_space.region_list != NULL)
            || (address_space.user_base != mm::UserVaBase)
            || (address_space.user_limit != mm::UserVaLimit)) {
            (void)mm::MemoryManager::destroy_user_address_space(&address_space);
            return StatusFault;
        }

        status = mm::MemoryManager::destroy_user_address_space(&address_space);
        if (status != StatusOK) {
            return status;
        }
        if ((address_space.page_table_root != 0U)
            || (address_space.translation_table_l0 != 0U)
            || (address_space.translation_table_l1 != 0U)
            || (address_space.translation_table_l2 != 0U)) {
            return StatusFault;
        }

        return StatusOK;
    }

    /**
     * Validate physical-page allocation, retain, and release accounting.
     *
     * The loader and user heap both depend on the refcounted page allocator, so
     * this smoke confirms the free-page watermark and refcount transitions stay
     * coherent across one full retain/release cycle.
     *
     * @return StatusOK when the page allocator returns to its original state.
     */
    Status run_physical_memory_smoke_test(void) {
        const unsigned int baseline_free_pages = mm::PhysicalMemory::free_page_count();
        PhysAddr page = mm::PhysicalMemory::alloc_page();

        if ((page == 0U) || !mm::PhysicalMemory::is_valid_phys_page(page)) {
            return StatusNoMemory;
        }
        if ((mm::PhysicalMemory::free_page_count() + 1U) != baseline_free_pages) {
            mm::PhysicalMemory::free_page(page);
            return StatusFault;
        }
        if (mm::PhysicalMemory::page_refcount(page) != 1U) {
            mm::PhysicalMemory::free_page(page);
            return StatusFault;
        }

        mm::PhysicalMemory::retain_page(page);
        if (mm::PhysicalMemory::page_refcount(page) != 2U) {
            mm::PhysicalMemory::free_page(page);
            mm::PhysicalMemory::free_page(page);
            return StatusFault;
        }

        mm::PhysicalMemory::free_page(page);
        if (mm::PhysicalMemory::page_refcount(page) != 1U) {
            mm::PhysicalMemory::free_page(page);
            return StatusFault;
        }

        mm::PhysicalMemory::free_page(page);
        if ((mm::PhysicalMemory::page_refcount(page) != 0U) || (mm::PhysicalMemory::free_page_count() != baseline_free_pages)) {
            return StatusFault;
        }

        return StatusOK;
    }

    /**
     * Validate one user-space page map/unmap roundtrip in addition to the MMU
     * self-test snapshot.
     *
     * The self-test proves the live kernel mappings are sane, while the extra
     * page-level exercise confirms fresh EL0 address spaces can still grow new
     * page-table leaves on demand.
     *
     * @param result_out Destination for the MMU self-test snapshot.
     * @return StatusOK when both the snapshot and mapping roundtrip succeed.
     */
    Status run_virtual_memory_smoke_test(VmSelfTestResult* result_out) {
        AddressSpace address_space = {};
        PhysAddr page = 0U;
        VmMapping mapping = {};
        const unsigned int baseline_free_pages = mm::PhysicalMemory::free_page_count();
        Status status;

        if (result_out == NULL) {
            return StatusInvalidArgument;
        }

        status = mm::MemoryManager::self_test(result_out);
        if (status != StatusOK) {
            return status;
        }

        status = mm::MemoryManager::create_user_address_space(&address_space);
        if (status != StatusOK) {
            return status;
        }

        page = mm::PhysicalMemory::alloc_page();
        if (page == 0U) {
            (void)mm::MemoryManager::destroy_user_address_space(&address_space);
            return StatusNoMemory;
        }

        mapping.virtual_base = VirtualMemorySmokeAddress;
        mapping.physical_base = page;
        mapping.length = mm::PageSize;
        mapping.flags = PagePresent | PageWritable | PageUser;
        status = mm::MemoryManager::map(&address_space, &mapping);
        if (status != StatusOK) {
            mm::PhysicalMemory::free_page(page);
            (void)mm::MemoryManager::destroy_user_address_space(&address_space);
            return status;
        }

        *reinterpret_cast<volatile U32*>(mm::MemoryManager::physical_to_kernel(page)) = 0x5A17A55AU;

        status = mm::MemoryManager::unmap(&address_space, mapping.virtual_base, mapping.length);
        if (status != StatusOK) {
            mm::PhysicalMemory::free_page(page);
            (void)mm::MemoryManager::destroy_user_address_space(&address_space);
            return status;
        }

        mm::PhysicalMemory::free_page(page);
        status = mm::MemoryManager::destroy_user_address_space(&address_space);
        if (status != StatusOK) {
            return status;
        }
        if (mm::PhysicalMemory::free_page_count() != baseline_free_pages) {
            return StatusFault;
        }

        return StatusOK;
    }

    /**
     * Validate the synthesized kernel-console input path.
     *
     * The `virt` board exposes a software injection queue that feeds the same
     * `Serial::getc()` path the console shell later uses, which makes it the
     * safest deterministic smoke for early keyboard input.
     *
     * @return StatusOK when the injected character is observed, or a logged
     * skip on boards without software input injection.
     */
    Status run_kernel_input_path_smoke_test(void) {
#if defined(BOARD_VIRT)
        static constexpr char ExpectedInput = 'Z';

        if (!board::Serial::inject_key(ExpectedInput)) {
            return StatusNoSpace;
        }
        if (board::Serial::getc() != ExpectedInput) {
            return StatusFault;
        }

        return StatusOK;
#else
        serial_write_text("[kernel] kernel input path skipped on this board\n");
        return StatusOK;
#endif
    }

    /**
     * Validate the character-device output path used by the kernel console.
     *
     * Going through the registered serial device rather than the raw board UART
     * proves the device layer and driver binding stay intact after platform
     * bring-up.
     *
     * @return StatusOK when one short serial write completes.
     */
    Status run_kernel_output_path_smoke_test(void) {
        static constexpr char OutputProbe[] = "[kernel-output-smoke]\n";
        Device* serial_device = DeviceManager::find_device("serial0");

        if (serial_device == NULL) {
            return StatusNotFound;
        }
        if (serial_device->write(0U, OutputProbe, COUNT_OF(OutputProbe) - 1U) != static_cast<SSize>(COUNT_OF(OutputProbe) - 1U)) {
            return StatusFault;
        }

        return StatusOK;
    }

    /**
     * Validate process event publication and subscription delivery.
     *
     * The broker sits on the lifecycle path for debugging and tooling, so this
     * smoke subscribes late and then forces one create/destroy pair to confirm
     * the queue receives the expected process records.
     *
     * @return StatusOK when both create and exit records are observed.
     */
    Status run_kernel_event_smoke_test(void) {
        KernelEventSubscriptionRequest request = {};
        KernelEventSubscriptionInfo info = {};
        KernelEventRecord records[4] = {};
        KernelEventSubscriptionId subscription_id = 0U;
        Process* process = NULL;
        Process* owner_process = Scheduler::current_process();
        U32 record_count = 0U;
        U64 owner_process_id;
        U64 process_id = 0U;
        bool saw_created = false;
        bool saw_exited = false;
        Status status;

        if (owner_process == NULL) {
            return StatusFault;
        }

        owner_process_id = owner_process->id;
        request.version = KERNEL_EVENT_ABI_VERSION;
        request.family_mask = KERNEL_EVENT_FAMILY_PROCESS;
        status = KernelEventBroker::subscribe(owner_process_id, &request, &subscription_id);
        if (status != StatusOK) {
            return status;
        }

        status = ProcessManager::create_process(KernelEventSmokeProcessName, &process);
        if (status == StatusOK) {
            process_id = process->id;
            status = ProcessManager::destroy_process(process);
        }
        if (status != StatusOK) {
            (void)KernelEventBroker::unsubscribe(owner_process_id, subscription_id);
            return status;
        }

        status = KernelEventBroker::query(owner_process_id, subscription_id, &info);
        if (status != StatusOK) {
            (void)KernelEventBroker::unsubscribe(owner_process_id, subscription_id);
            return status;
        }
        if (info.queued_count == 0U) {
            (void)KernelEventBroker::unsubscribe(owner_process_id, subscription_id);
            return StatusFault;
        }

        status = KernelEventBroker::read(owner_process_id, subscription_id, records, COUNT_OF(records), &record_count);
        (void)KernelEventBroker::unsubscribe(owner_process_id, subscription_id);
        if (status != StatusOK) {
            return status;
        }

        for (U32 index = 0U; index < record_count; ++index) {
            if (records[index].source_process_id != process_id) {
                continue;
            }

            if (records[index].type == KernelEventTypeProcessCreated) {
                saw_created = true;
            }
            else if (records[index].type == KernelEventTypeProcessExited) {
                saw_exited = true;
            }
        }

        return (saw_created && saw_exited) ? StatusOK : StatusFault;
    }

    /**
     * Validate named shared-memory creation, reopen, and teardown semantics.
     *
     * The smoke checks the manager's key contract: one process can reopen an
     * existing mapping idempotently, another process can attach by name, and
     * the object disappears once the last attachment owner exits.
     *
     * @return StatusOK when the shared-memory lifecycle behaves as expected.
     */
    Status run_shared_memory_smoke_test(void) {
        Process* first_process = NULL;
        Process* second_process = NULL;
        Process* third_process = NULL;
        VirtAddr first_address = 0U;
        VirtAddr first_address_again = 0U;
        VirtAddr second_address = 0U;
        VirtAddr third_address = 0U;
        Status status = SharedMemoryManager::init();

        if (status != StatusOK) {
            return status;
        }

        status = ProcessManager::create_user_process("shm-smoke-a", &first_process);
        if (status != StatusOK) {
            return status;
        }

        status = ProcessManager::create_user_process("shm-smoke-b", &second_process);
        if (status != StatusOK) {
            (void)ProcessManager::destroy_process(first_process);
            return status;
        }

        status = SharedMemoryManager::acquire(first_process, SharedMemorySmokeName, 128U, &first_address);
        if (status != StatusOK) {
            (void)ProcessManager::destroy_process(second_process);
            (void)ProcessManager::destroy_process(first_process);
            return status;
        }

        status = SharedMemoryManager::acquire(first_process, SharedMemorySmokeName, 0U, &first_address_again);
        if (status != StatusOK) {
            (void)ProcessManager::destroy_process(second_process);
            (void)ProcessManager::destroy_process(first_process);
            return status;
        }

        status = SharedMemoryManager::acquire(second_process, SharedMemorySmokeName, 0U, &second_address);
        if (status != StatusOK) {
            (void)ProcessManager::destroy_process(second_process);
            (void)ProcessManager::destroy_process(first_process);
            return status;
        }
        if ((first_address == 0U) || (first_address_again != first_address) || (second_address == 0U)) {
            (void)ProcessManager::destroy_process(second_process);
            (void)ProcessManager::destroy_process(first_process);
            return StatusFault;
        }

        status = ProcessManager::destroy_process(second_process);
        if (status != StatusOK) {
            (void)ProcessManager::destroy_process(first_process);
            return status;
        }

        status = ProcessManager::destroy_process(first_process);
        if (status != StatusOK) {
            return status;
        }

        status = ProcessManager::create_user_process("shm-smoke-c", &third_process);
        if (status != StatusOK) {
            return status;
        }

        status = SharedMemoryManager::acquire(third_process, SharedMemorySmokeName, 0U, &third_address);
        if (status != StatusNotFound) {
            (void)ProcessManager::destroy_process(third_process);
            return StatusFault;
        }
        if (third_address != 0U) {
            (void)ProcessManager::destroy_process(third_process);
            return StatusFault;
        }

        return ProcessManager::destroy_process(third_process);
    }

    /**
     * Validate the scheduler's basic context-switch, ready-queue, and priority
     * ordering paths as one grouped boot suite.
     *
     * The individual helpers already isolate the failure details, so the group
     * wrapper only preserves the staged progress messages that were useful in
     * the previous fixed boot sequence.
     *
     * @return StatusOK when every scheduler subtest passes.
     */
    Status run_scheduler_boot_test(void) {
        Status status = ThreadManager::create_thread(
            Scheduler::current_process(),
            "sched-smoke",
            reinterpret_cast<VirtAddr>(&scheduler_smoke_entry),
            &g_scheduler_stress_threads[0]);

        if (status != StatusOK) {
            return status;
        }

        g_scheduler_smoke_complete = false;
        status = Scheduler::enqueue(g_scheduler_stress_threads[0]);
        if (status != StatusOK) {
            return status;
        }

        Scheduler::yield();
        if (!g_scheduler_smoke_complete) {
            return StatusFault;
        }

        Scheduler::poll();
        board::Serial::puts("[kernel] scheduler switch ready\n");

        status = run_scheduler_stress_test();
        if (status != StatusOK) {
            return status;
        }
        board::Serial::puts("[kernel] scheduler 1000-thread test ready\n");

        status = run_scheduler_priority_smoke_test();
        if (status != StatusOK) {
            return status;
        }
        board::Serial::puts("[kernel] scheduler priority queues ready\n");

        return StatusOK;
    }

    /**
     * Validate that the board registered the expected core devices and a sane
     * timer source before userspace starts depending on them.
     *
     * @return StatusOK when the essential platform probes succeed.
     */
    Status run_platform_smoke_test(void) {
        Device* serial_device = DeviceManager::find_device("serial0");
        Device* disk_device = DeviceManager::find_device("disk0");
        Device* framebuffer_device = DeviceManager::find_device("framebuffer0");
        FramebufferGeometry geometry = {};

        if ((serial_device == NULL) || (disk_device == NULL) || (arch::Arch::counter_frequency() == 0ULL)) {
            return StatusFault;
        }
        if (framebuffer_device == NULL) {
            return StatusOK;
        }
        if (framebuffer_device->ioctl(FramebufferIoctlGetGeometry, &geometry) != StatusOK) {
            return StatusFault;
        }
        if ((geometry.width == 0U)
            || (geometry.height == 0U)
            || (geometry.bytes_per_pixel == 0U)
            || (geometry.size_bytes < (static_cast<U64>(geometry.width) * static_cast<U64>(geometry.height) * static_cast<U64>(geometry.bytes_per_pixel)))) {
            return StatusFault;
        }

        return StatusOK;
    }

    Status vfs_smoke_enumerate_root(const char* name, const VfsNode* node, void* context) {
        VfsSmokeEnumerationContext* enumeration = static_cast<VfsSmokeEnumerationContext*>(context);

        (void)node;
        if ((enumeration == NULL) || (name == NULL)) {
            return StatusInvalidArgument;
        }

        if (same_text_case_insensitive(name, "kernel")) {
            enumeration->saw_kernel = true;
        }
        else if (same_text_case_insensitive(name, "Long File Name.txt")) {
            enumeration->saw_long_name_sample = true;
        }
        else if (same_text_case_insensitive(name, "Writable Sample.txt")) {
            enumeration->saw_writable_sample = true;
        }

        return StatusOK;
    }

    Status vfs_find_name_visitor(const char* name, const VfsNode* node, void* context) {
        VfsNameCheckContext* name_check = static_cast<VfsNameCheckContext*>(context);

        (void)node;
        if ((name_check == NULL) || (name == NULL) || (name_check->expected_name == NULL)) {
            return StatusInvalidArgument;
        }

        if (same_text_case_insensitive(name, name_check->expected_name)) {
            name_check->found = true;
        }

        return StatusOK;
    }

    Status list_sdcard_directory(const VfsNode* directory, Size depth);

    Status list_sdcard_visitor(const char* name, const VfsNode* node, void* context) {
        SdcardListContext* list_context = static_cast<SdcardListContext*>(context);

        if ((list_context == NULL) || (name == NULL) || (node == NULL)) {
            return StatusInvalidArgument;
        }

        serial_write_indent(list_context->depth);
        if (node->type == VfsNodeTypeDirectory) {
            serial_write_text("[dir] ");
            serial_write_text(name);
            serial_write_char('\n');
            return list_sdcard_directory(node, list_context->depth + 1U);
        }

        serial_write_text("[file] ");
        serial_write_text(name);
        serial_write_text(" (");
        serial_write_decimal(node->size_bytes);
        serial_write_text(" bytes)\n");
        return StatusOK;
    }

    Status list_sdcard_directory(const VfsNode* directory, Size depth) {
        SdcardListContext list_context = { depth };

        return VirtualFileSystem::enumerate(directory, &list_context, &list_sdcard_visitor);
    }

    void list_sdcard_process_main(void) {
        VfsNode root_node;
        Status status = VirtualFileSystem::resolve("C:\\", &root_node);

        serial_write_text("[list-sdcard] C:\\\n");
        if (status != StatusOK) {
            serial_write_text("[list-sdcard] unable to resolve C:\\\n");
            g_list_sdcard_process_running = false;
            scheduler_thread_exit_current();
        }

        status = list_sdcard_directory(&root_node, 1U);
        if (status != StatusOK) {
            serial_write_text("[list-sdcard] enumeration failed\n");
        }
        g_list_sdcard_process_running = false;
        scheduler_thread_exit_current();
    }

    Status start_list_sdcard_process(void) {
        Process* process = NULL;
        Thread* thread = NULL;
        Status status;

        if (g_list_sdcard_process_running) {
            return StatusBusy;
        }

        g_list_sdcard_process_running = true;
        status = ProcessManager::create_process("list-sdcard", &process);
        if (status != StatusOK) {
            g_list_sdcard_process_running = false;
            return status;
        }

        status = ThreadManager::create_thread(process, "main", reinterpret_cast<VirtAddr>(&list_sdcard_process_main), &thread);
        if (status != StatusOK) {
            (void)ProcessManager::destroy_process(process);
            g_list_sdcard_process_running = false;
            return status;
        }

        status = Scheduler::enqueue(thread);
        if (status != StatusOK) {
            (void)ProcessManager::destroy_process(process);
            g_list_sdcard_process_running = false;
            return status;
        }

        return StatusOK;
    }

    void print_framebuffer_info(void) {
        VfsNode framebuffer_node;
        FramebufferGeometry geometry = {};
        Status status = VirtualFileSystem::resolve("FRAMEBUFFER:", &framebuffer_node);

        if ((status != StatusOK) || (framebuffer_node.device == NULL)) {
            serial_write_text("FRAMEBUFFER unavailable\n");
            return;
        }

        status = framebuffer_node.device->ioctl(FramebufferIoctlGetGeometry, &geometry);
        if (status != StatusOK) {
            serial_write_text("FRAMEBUFFER geometry unavailable\n");
            return;
        }

        serial_write_text("FRAMEBUFFER ");
        serial_write_decimal(geometry.width);
        serial_write_text("x");
        serial_write_decimal(geometry.height);
        serial_write_text(" pitch=");
        serial_write_decimal(geometry.pitch);
        serial_write_text(" bpp=");
        serial_write_decimal(geometry.bytes_per_pixel);
        serial_write_char('\n');
    }

    bool build_indexed_name(const char* prefix, Size index, char* output, Size capacity) {
        char digits[21];
        Size prefix_length = 0U;
        Size digit_count = 0U;
        Size write_index = 0U;

        if ((prefix == NULL) || (output == NULL) || (capacity == 0U)) {
            return false;
        }

        while (prefix[prefix_length] != '\0') {
            if ((prefix_length + 1U) >= capacity) {
                return false;
            }
            output[prefix_length] = prefix[prefix_length];
            ++prefix_length;
        }

        do {
            digits[digit_count++] = static_cast<char>('0' + (index % 10U));
            index /= 10U;
        } while (index != 0U);

        if ((prefix_length + digit_count + 1U) > capacity) {
            return false;
        }

        write_index = prefix_length;
        while (digit_count > 0U) {
            output[write_index++] = digits[--digit_count];
        }
        output[write_index] = '\0';
        return true;
    }

    bool bytes_equal(const U8* lhs, const U8* rhs, Size length) {
        if ((lhs == NULL) || (rhs == NULL)) {
            return false;
        }

        for (Size index = 0; index < length; ++index) {
            if (lhs[index] != rhs[index]) {
                return false;
            }
        }

        return true;
    }

    Status register_alias(Device* device, const char* alias) {
        Status status;

        if ((device == NULL) || (alias == NULL)) {
            return StatusInvalidArgument;
        }

        status = VirtualFileSystem::register_device_name(alias, device);
        if (status == StatusAlreadyExists) {
            return StatusOK;
        }
        return status;
    }

    Status register_serial_device_aliases(void) {
        char device_name[16];
        char alias_name[16];

        for (Size index = 0U; index < VfsAliasScanLimit; ++index) {
            Device* serial_device;
            Status status;

            if (!build_indexed_name("serial", index, device_name, sizeof(device_name))) {
                return StatusNoSpace;
            }

            serial_device = DeviceManager::find_device(device_name);
            if (serial_device == NULL) {
                continue;
            }

            status = serial_device->init();
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            if (!build_indexed_name("COM", index + 1U, alias_name, sizeof(alias_name))) {
                return StatusNoSpace;
            }
            status = register_alias(serial_device, alias_name);
            if (status != StatusOK) {
                return status;
            }

            if (index == 0U) {
                status = register_alias(serial_device, "UART");
                if (status != StatusOK) {
                    return status;
                }
            }

            if (!build_indexed_name("UART", index, alias_name, sizeof(alias_name))) {
                return StatusNoSpace;
            }
            status = register_alias(serial_device, alias_name);
            if (status != StatusOK) {
                return status;
            }
        }

        return StatusOK;
    }

    Status register_framebuffer_device_aliases(void) {
        bool generic_alias_registered = false;
        char device_name[24];
        char alias_name[24];

        for (Size index = 0U; index < VfsAliasScanLimit; ++index) {
            Device* framebuffer_device;
            Status status;

            if (!build_indexed_name("framebuffer", index, device_name, sizeof(device_name))) {
                return StatusNoSpace;
            }

            framebuffer_device = DeviceManager::find_device(device_name);
            if (framebuffer_device == NULL) {
                continue;
            }

            status = framebuffer_device->init();
            if (status == StatusNotFound || status == StatusNotSupported) {
                continue;
            }
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            if (!generic_alias_registered) {
                status = register_alias(framebuffer_device, "FRAMEBUFFER");
                if (status != StatusOK) {
                    return status;
                }
                generic_alias_registered = true;
            }

            if (!build_indexed_name("FRAMEBUFFER", index, alias_name, sizeof(alias_name))) {
                return StatusNoSpace;
            }
            status = register_alias(framebuffer_device, alias_name);
            if (status != StatusOK) {
                return status;
            }
        }

        return StatusOK;
    }

    Status initialize_vfs_namespace(void) {
        Device* serial_device = DeviceManager::find_device("serial0");
        Device* disk_device = DeviceManager::find_device("disk0");
        VfsMount system_mount = {
            'C',
            filesystem::Fat32Filesystem::driver(),
            filesystem::Fat32Filesystem::system_volume_state(),
            disk_device,
        };
        Status status;

        if ((serial_device == NULL) || (disk_device == NULL)) {
            return StatusNotFound;
        }

        status = disk_device->init();
        if ((status != StatusOK) && (status != StatusAlreadyExists)) {
            return status;
        }

        status = FilesystemRegistry::init();
        if (status != StatusOK) {
            return status;
        }

        status = FilesystemRegistry::register_driver(filesystem::Fat32Filesystem::driver());
        if ((status != StatusOK) && (status != StatusAlreadyExists)) {
            return status;
        }

        status = VirtualFileSystem::init();
        if (status != StatusOK) {
            return status;
        }

        status = VirtualFileSystem::mount_root(&system_mount);
        if ((status != StatusOK) && (status != StatusAlreadyExists)) {
            return status;
        }

        status = register_serial_device_aliases();
        if (status != StatusOK) {
            return status;
        }

        return register_framebuffer_device_aliases();
    }

    Status run_vfs_namespace_smoke_test(void) {
        Device* serial_device = DeviceManager::find_device("serial0");
        Device* framebuffer_device = DeviceManager::find_device("framebuffer0");
        VfsSmokeEnumerationContext enumeration = {};
        VfsNameCheckContext created_name = { CreateRemoveFileName, false };
        VfsNode node;
        VfsNode created_directory_node;
        VfsNode created_file_node;
        Status status;
        U8 kernel_header[sizeof(KernelImageMagic)];
        U8 long_name_text[COUNT_OF(LongFileNameSampleText) - 1U];
        U8 writable_text[COUNT_OF(WritableSampleOriginalText) - 1U];
        U8 created_text[COUNT_OF(CreateRemoveFileText) - 1U];
        FramebufferGeometry geometry = {};

        if (serial_device == NULL) {
            return StatusNotFound;
        }

        status = VirtualFileSystem::resolve("C:\\", &node);
        if ((status != StatusOK) || (node.backend_kind != VfsBackendKindFilesystem) || (node.type != VfsNodeTypeDirectory) || (node.volume_letter != 'C')) {
            return StatusFault;
        }

        status = VirtualFileSystem::enumerate(&node, &enumeration, &vfs_smoke_enumerate_root);
        if ((status != StatusOK) || !enumeration.saw_kernel || !enumeration.saw_long_name_sample || !enumeration.saw_writable_sample) {
            return StatusFault;
        }

        status = VirtualFileSystem::resolve("C:\\kernel", &node);
        if ((status != StatusOK) || (node.backend_kind != VfsBackendKindFilesystem) || (node.type != VfsNodeTypeFile)) {
            return StatusFault;
        }

        memzero(kernel_header, sizeof(kernel_header));
        if (VirtualFileSystem::read(&node, 0U, kernel_header, sizeof(kernel_header)) != static_cast<SSize>(sizeof(kernel_header))) {
            return StatusFault;
        }
        if (!bytes_equal(kernel_header, KernelImageMagic, sizeof(KernelImageMagic))) {
            return StatusFault;
        }

        status = VirtualFileSystem::resolve(LongFileNameSamplePath, &node);
        if ((status != StatusOK) || (node.type != VfsNodeTypeFile)) {
            return StatusFault;
        }

        memzero(long_name_text, sizeof(long_name_text));
        if (VirtualFileSystem::read(&node, 0U, long_name_text, sizeof(long_name_text)) != static_cast<SSize>(sizeof(long_name_text))) {
            return StatusFault;
        }
        if (!bytes_equal(long_name_text, reinterpret_cast<const U8*>(LongFileNameSampleText), sizeof(long_name_text))) {
            return StatusFault;
        }

        status = VirtualFileSystem::resolve(WritableSamplePath, &node);
        if ((status != StatusOK) || (node.type != VfsNodeTypeFile)) {
            return StatusFault;
        }
        if (VirtualFileSystem::write(&node, 0U, WritableSampleUpdatedText, COUNT_OF(WritableSampleUpdatedText) - 1U)
            != static_cast<SSize>(COUNT_OF(WritableSampleUpdatedText) - 1U)) {
            return StatusFault;
        }

        memzero(writable_text, sizeof(writable_text));
        if (VirtualFileSystem::read(&node, 0U, writable_text, COUNT_OF(WritableSampleUpdatedText) - 1U) != static_cast<SSize>(COUNT_OF(WritableSampleUpdatedText) - 1U)) {
            return StatusFault;
        }
        if (!bytes_equal(writable_text, reinterpret_cast<const U8*>(WritableSampleUpdatedText), COUNT_OF(WritableSampleUpdatedText) - 1U)) {
            return StatusFault;
        }

        // Restore the shared writable fixture so later userspace tests observe the staged
        // baseline contents instead of the kernel smoke's temporary overwrite.
        if (VirtualFileSystem::write(&node, 0U, WritableSampleOriginalText, COUNT_OF(WritableSampleOriginalText) - 1U)
            != static_cast<SSize>(COUNT_OF(WritableSampleOriginalText) - 1U)) {
            return StatusFault;
        }

        memzero(writable_text, sizeof(writable_text));
        if (VirtualFileSystem::read(&node, 0U, writable_text, sizeof(writable_text)) != static_cast<SSize>(sizeof(writable_text))) {
            return StatusFault;
        }
        if (!bytes_equal(writable_text, reinterpret_cast<const U8*>(WritableSampleOriginalText), sizeof(writable_text))) {
            return StatusFault;
        }

        // Clean up any leftover smoke-test entries from an earlier interrupted boot before recreating them.
        (void)VirtualFileSystem::remove(CreateRemoveFilePath);
        (void)VirtualFileSystem::remove(CreateRemoveDirectoryPath);

        status = VirtualFileSystem::create(CreateRemoveDirectoryPath, VfsNodeTypeDirectory, &created_directory_node);
        if ((status != StatusOK) || (created_directory_node.type != VfsNodeTypeDirectory)) {
            return StatusFault;
        }

        status = VirtualFileSystem::create(CreateRemoveFilePath, VfsNodeTypeFile, &created_file_node);
        if ((status != StatusOK) || (created_file_node.type != VfsNodeTypeFile)) {
            return StatusFault;
        }

        if (VirtualFileSystem::write(&created_file_node, 0U, CreateRemoveFileText, COUNT_OF(CreateRemoveFileText) - 1U)
            != static_cast<SSize>(COUNT_OF(CreateRemoveFileText) - 1U)) {
            return StatusFault;
        }

        status = VirtualFileSystem::resolve(CreateRemoveFilePath, &created_file_node);
        if ((status != StatusOK) || (created_file_node.type != VfsNodeTypeFile)) {
            return StatusFault;
        }

        memzero(created_text, sizeof(created_text));
        if (VirtualFileSystem::read(&created_file_node, 0U, created_text, sizeof(created_text)) != static_cast<SSize>(sizeof(created_text))) {
            return StatusFault;
        }
        if (!bytes_equal(created_text, reinterpret_cast<const U8*>(CreateRemoveFileText), sizeof(created_text))) {
            return StatusFault;
        }

        status = VirtualFileSystem::resolve(CreateRemoveDirectoryPath, &created_directory_node);
        if ((status != StatusOK) || (created_directory_node.type != VfsNodeTypeDirectory)) {
            return StatusFault;
        }
        status = VirtualFileSystem::enumerate(&created_directory_node, &created_name, &vfs_find_name_visitor);
        if ((status != StatusOK) || !created_name.found) {
            return StatusFault;
        }

        status = VirtualFileSystem::remove(CreateRemoveDirectoryPath);
        if (status != StatusBusy) {
            return StatusFault;
        }

        status = VirtualFileSystem::remove(CreateRemoveFilePath);
        if (status != StatusOK) {
            return StatusFault;
        }
        status = VirtualFileSystem::resolve(CreateRemoveFilePath, &node);
        if (status != StatusNotFound) {
            return StatusFault;
        }

        status = VirtualFileSystem::remove(CreateRemoveDirectoryPath);
        if (status != StatusOK) {
            return StatusFault;
        }
        status = VirtualFileSystem::resolve(CreateRemoveDirectoryPath, &node);
        if (status != StatusNotFound) {
            return StatusFault;
        }

        status = VirtualFileSystem::resolve("C:\\missing", &node);
        if (status != StatusNotFound) {
            return StatusFault;
        }

        status = VirtualFileSystem::resolve("COM1:", &node);
        if ((status != StatusOK) || (node.backend_kind != VfsBackendKindDevice) || (node.type != VfsNodeTypeDevice) || (node.device != serial_device)) {
            return StatusFault;
        }

        status = VirtualFileSystem::resolve("UART:", &node);
        if ((status != StatusOK) || (node.backend_kind != VfsBackendKindDevice) || (node.device != serial_device)) {
            return StatusFault;
        }

        status = VirtualFileSystem::resolve("COM3:", &node);
        if (status != StatusNotFound) {
            return StatusFault;
        }

        if (framebuffer_device != NULL) {
            status = framebuffer_device->init();
            if ((status == StatusOK) || (status == StatusAlreadyExists)) {
                status = VirtualFileSystem::resolve("FRAMEBUFFER:", &node);
                if ((status != StatusOK) || (node.backend_kind != VfsBackendKindDevice) || (node.device != framebuffer_device)) {
                    return StatusFault;
                }
                status = node.device->ioctl(FramebufferIoctlGetGeometry, &geometry);
                if ((status != StatusOK) || (geometry.width == 0U) || (geometry.height == 0U) || (geometry.pitch == 0U)
                    || (geometry.bytes_per_pixel == 0U) || (geometry.size_bytes != (static_cast<U64>(geometry.pitch) * geometry.height))) {
                    return StatusFault;
                }
            }
        }

        return StatusOK;
    }

    Size scheduler_stress_index_for_thread(const Thread* thread) {
        if (thread == NULL) {
            return SchedulerStressThreadCount;
        }

        for (Size index = 0; index < SchedulerStressThreadCount; ++index) {
            if (g_scheduler_stress_threads[index] == thread) {
                return index;
            }
        }

        return SchedulerStressThreadCount;
    }

    Size scheduler_priority_index_for_thread(const Thread* thread) {
        if (thread == NULL) {
            return SchedulerPrioritySmokeThreadCount;
        }

        for (Size index = 0U; index < SchedulerPrioritySmokeThreadCount; ++index) {
            if (g_scheduler_priority_threads[index] == thread) {
                return index;
            }
        }

        return SchedulerPrioritySmokeThreadCount;
    }

    void scheduler_priority_record_event(char event_code) {
        if (g_scheduler_priority_event_count >= COUNT_OF(g_scheduler_priority_events)) {
            g_scheduler_priority_failed = true;
            return;
        }

        g_scheduler_priority_events[g_scheduler_priority_event_count++] = event_code;
    }

    void scheduler_priority_entry(void) {
        Size index = scheduler_priority_index_for_thread(Scheduler::current());

        if (index == SchedulerPrioritySmokeThreadCount) {
            g_scheduler_priority_failed = true;
            return;
        }
        if (g_scheduler_priority_phases[index] != 0U) {
            g_scheduler_priority_failed = true;
            return;
        }

        g_scheduler_priority_phases[index] = 1U;
        switch (index) {
        case 0U:
            scheduler_priority_record_event('A');
            Scheduler::yield();
            if (g_scheduler_priority_phases[index] != 1U) {
                g_scheduler_priority_failed = true;
                return;
            }
            scheduler_priority_record_event('a');
            break;
        case 1U:
            scheduler_priority_record_event('B');
            Scheduler::yield();
            if (g_scheduler_priority_phases[index] != 1U) {
                g_scheduler_priority_failed = true;
                return;
            }
            scheduler_priority_record_event('b');
            break;
        case 2U:
            scheduler_priority_record_event('M');
            break;
        case 3U:
            scheduler_priority_record_event('L');
            break;
        default:
            g_scheduler_priority_failed = true;
            return;
        }

        g_scheduler_priority_phases[index] = 2U;
        ++g_scheduler_priority_completed_count;
    }

    void scheduler_stress_entry(void) {
        Size index = scheduler_stress_index_for_thread(Scheduler::current());

        if (index == SchedulerStressThreadCount) {
            g_scheduler_stress_failed = true;
            return;
        }
        if (g_scheduler_stress_run_counts[index] != 0U) {
            g_scheduler_stress_failed = true;
            return;
        }

        g_scheduler_stress_run_counts[index] = 1U;
        ++g_scheduler_stress_first_pass_count;

        // Every stress worker yields once so the test proves ready-queue rotation and requeueing,
        // not just one-shot thread creation followed by immediate exit.
        Scheduler::yield();

        if (g_scheduler_stress_run_counts[index] != 1U) {
            g_scheduler_stress_failed = true;
            return;
        }

        g_scheduler_stress_run_counts[index] = 2U;
        ++g_scheduler_stress_completed_count;
    }

    Status run_scheduler_stress_test(void) {
        Thread* thread;

        g_scheduler_stress_failed = false;
        g_scheduler_stress_first_pass_count = 0U;
        g_scheduler_stress_completed_count = 0U;

        for (Size index = 0; index < SchedulerStressThreadCount; ++index) {
            g_scheduler_stress_threads[index] = NULL;
            g_scheduler_stress_run_counts[index] = 0U;
        }

        for (Size index = 0; index < SchedulerStressThreadCount; ++index) {
            Status status = ThreadManager::create_thread(
                Scheduler::current_process(),
                "sched-stress",
                reinterpret_cast<VirtAddr>(&scheduler_stress_entry),
                &thread);
            if (status != StatusOK) {
                return status;
            }

            g_scheduler_stress_threads[index] = thread;
            status = Scheduler::enqueue(thread);
            if (status != StatusOK) {
                return status;
            }
        }

        while (g_scheduler_stress_completed_count != SchedulerStressThreadCount) {
            if (g_scheduler_stress_failed) {
                return StatusFault;
            }

            Scheduler::yield();
            Scheduler::poll();
        }

        if (g_scheduler_stress_first_pass_count != SchedulerStressThreadCount) {
            return StatusFault;
        }

        for (Size index = 0; index < SchedulerStressThreadCount; ++index) {
            if (g_scheduler_stress_run_counts[index] != 2U) {
                return StatusFault;
            }
        }
        if (ThreadManager::thread_count() != 2U) {
            return StatusFault;
        }

        return StatusOK;
    }

    Status run_scheduler_priority_smoke_test(void) {
        constexpr char ExpectedOrder[] = { 'A', 'B', 'a', 'b', 'M', 'L' };

        Thread* thread;
        Status status;
        Size spin_count = 0U;

        g_scheduler_priority_failed = false;
        g_scheduler_priority_event_count = 0U;
        g_scheduler_priority_completed_count = 0U;
        memzero(g_scheduler_priority_threads, sizeof(g_scheduler_priority_threads));
        memzero(g_scheduler_priority_phases, sizeof(g_scheduler_priority_phases));
        memzero(g_scheduler_priority_events, sizeof(g_scheduler_priority_events));

        status = ThreadManager::create_thread(
            Scheduler::current_process(),
            "prio-high-a",
            reinterpret_cast<VirtAddr>(&scheduler_priority_entry),
            &thread,
            4U,
            ThreadDefaultQuantumTicks);
        if (status != StatusOK) {
            return status;
        }
        g_scheduler_priority_threads[0] = thread;

        status = ThreadManager::create_thread(
            Scheduler::current_process(),
            "prio-high-b",
            reinterpret_cast<VirtAddr>(&scheduler_priority_entry),
            &thread,
            4U,
            ThreadDefaultQuantumTicks);
        if (status != StatusOK) {
            return status;
        }
        g_scheduler_priority_threads[1] = thread;

        status = ThreadManager::create_thread(
            Scheduler::current_process(),
            "prio-medium",
            reinterpret_cast<VirtAddr>(&scheduler_priority_entry),
            &thread,
            10U,
            ThreadDefaultQuantumTicks);
        if (status != StatusOK) {
            return status;
        }
        g_scheduler_priority_threads[2] = thread;

        status = ThreadManager::create_thread(
            Scheduler::current_process(),
            "prio-low",
            reinterpret_cast<VirtAddr>(&scheduler_priority_entry),
            &thread,
            15U,
            ThreadDefaultQuantumTicks);
        if (status != StatusOK) {
            return status;
        }
        g_scheduler_priority_threads[3] = thread;

        // Enqueue out of priority order so the test proves bitmap selection beats insertion order.
        status = Scheduler::enqueue(g_scheduler_priority_threads[3]);
        if (status != StatusOK) {
            return status;
        }
        status = Scheduler::enqueue(g_scheduler_priority_threads[0]);
        if (status != StatusOK) {
            return status;
        }
        status = Scheduler::enqueue(g_scheduler_priority_threads[2]);
        if (status != StatusOK) {
            return status;
        }
        status = Scheduler::enqueue(g_scheduler_priority_threads[1]);
        if (status != StatusOK) {
            return status;
        }

        while (g_scheduler_priority_completed_count != SchedulerPrioritySmokeThreadCount) {
            if (g_scheduler_priority_failed) {
                board::Serial::puts("[sched-prio] worker failure\n");
                return StatusFault;
            }
            if (spin_count++ > 4096U) {
                board::Serial::puts("[sched-prio] timeout\n");
                return StatusFault;
            }

            Scheduler::yield();
            Scheduler::poll();
        }

        if (g_scheduler_priority_event_count != COUNT_OF(ExpectedOrder)) {
            board::Serial::puts("[sched-prio] event count mismatch\n");
            return StatusFault;
        }
        for (Size index = 0U; index < COUNT_OF(ExpectedOrder); ++index) {
            if (g_scheduler_priority_events[index] != ExpectedOrder[index]) {
                board::Serial::puts("[sched-prio] event order mismatch\n");
                return StatusFault;
            }
        }

        // The last helper thread can retire on the same handoff that returns control to the boot
        // thread, so force one more scheduler pass before asserting the steady-state thread count.
        Scheduler::yield();
        Scheduler::poll();
        if (ThreadManager::thread_count() != 2U) {
            serial_write_text("[sched-prio] thread leak count=");
            serial_write_decimal(ThreadManager::thread_count());
            serial_write_text(" ready=");
            serial_write_decimal(Scheduler::ready_count());
            serial_write_char('\n');
            return StatusFault;
        }

        return StatusOK;
    }

    [[noreturn]] void halt(void) {
        board::Platform::power_off();
    }

} // namespace

class ConsoleSession final {
public:
    explicit ConsoleSession(Device* console_device)
        : console_device_(console_device) {
    }

    void run() const {
        char line[VFS_PATH_CAPACITY];
        Size line_length = 0U;

        memzero(line, sizeof(line));
        prompt();

        for (;;) {
            char ch = read_char();

            if (ch == '\r') {
                ch = '\n';
            }
            if (ch == '\n') {
                write_char('\n');
                line[line_length] = '\0';
                execute_command(line);
                line_length = 0U;
                line[0] = '\0';
                prompt();
                continue;
            }
            if ((ch == '\b') || (ch == 0x7f)) {
                if (line_length != 0U) {
                    --line_length;
                    line[line_length] = '\0';
                    write_text("\b \b");
                }
                continue;
            }

            if ((line_length + 1U) >= COUNT_OF(line)) {
                continue;
            }

            line[line_length++] = ch;
            line[line_length] = '\0';
            write_char(ch);
        }
    }

    void write_text(const char* text) const {
        while ((text != NULL) && (*text != '\0')) {
            write_char(*text++);
        }
    }

    void write_bool(bool value) const {
        write_text(value ? "yes" : "no");
    }

    void write_hex(uint64_t value) const {
        char buffer[17];

        for (int index = 0; index < 16; ++index) {
            uint8_t nibble = (value >> ((15 - index) * 4)) & 0xF;
            buffer[index] = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
        }

        buffer[16] = '\0';
        write_text(buffer);
    }

private:
    void print_help() const {
        write_text("help\n");
        write_text("list sdcard\n");
        write_text("fbinfo\n");
    }

    void execute_command(char* line) const {
        char* cursor = line;
        char command[32];
        char argument[32];
        char extra[32];

        if (!next_token(&cursor, command, sizeof(command))) {
            return;
        }

        if (same_text_case_insensitive(command, "help")) {
            if (!next_token(&cursor, extra, sizeof(extra))) {
                print_help();
                return;
            }
        }

        if (same_text_case_insensitive(command, "fbinfo")) {
            if (!next_token(&cursor, extra, sizeof(extra))) {
                print_framebuffer_info();
                return;
            }
        }

        if (same_text_case_insensitive(command, "list")) {
            if (next_token(&cursor, argument, sizeof(argument))
                && same_text_case_insensitive(argument, "sdcard")
                && !next_token(&cursor, extra, sizeof(extra))) {
                Status status = start_list_sdcard_process();

                if (status != StatusOK) {
                    write_text("list sdcard unavailable\n");
                    return;
                }

                Scheduler::yield();
                Scheduler::poll();
                return;
            }
        }

        write_text("Unknown command. Type 'help'.\n");
    }

    void prompt() const {
        write_text("> ");
    }

    char read_char() const {
        char ch = '\0';

        if (console_device_->read(0, &ch, sizeof(ch)) != (SSize)sizeof(ch)) {
            halt();
        }

        return ch;
    }

    void write_char(char ch) const {
        if (console_device_->write(0, &ch, sizeof(ch)) != (SSize)sizeof(ch)) {
            halt();
        }
    }

    Device* console_device_;
};

extern "C" void kernel_main(void) {
    Device* console_device;
    Status status;
    VmSelfTestResult vm_test = {};
    bool vm_test_ran = false;

    status = board::Platform::early_init();
    if (status != StatusOK) {
        halt();
    }

    status = arch::Arch::early_init(NULL);
    if (status != StatusOK) {
        halt();
    }

    kernel_debug_initialize();
    status = configure_kernel_debug_boot_settings();
    if (status != StatusOK) {
        halt();
    }
    board::Serial::puts("[kernel] early init\n");

    status = mm::MemoryManager::init();
    if (status != StatusOK) {
        halt();
    }

    board::Serial::puts("[kernel] mm init\n");

    status = mm::PhysicalMemory::init();
    if (status != StatusOK) {
        halt();
    }


    status = Heap::init();
    if (status != StatusOK) {
        halt();
    }

    board::Serial::puts("[kernel] heap ready\n");

    status = run_optional_boot_test("heap stress", g_kernel_boot_test_config.heap, &run_heap_stress_test, "[kernel] heap stress ready\n");
    if (status != StatusOK) {
        halt_with_status("heap stress", status);
    }

    // Bring up the core ownership model before any later subsystem starts creating processes or threads.
    status = KernelObjectManager::init();
    if (status != StatusOK) {
        halt();
    }

    status = ProcessManager::init();
    if (status != StatusOK) {
        halt();
    }

    status = ThreadManager::init();
    if (status != StatusOK) {
        halt();
    }

    status = KernelEventBroker::init();
    if (status != StatusOK) {
        halt();
    }

    board::Serial::puts("[kernel] core managers ready\n");

    status = Scheduler::init();
    if (status != StatusOK) {
        board::Serial::puts("[kernel] scheduler init failed\n");
        halt();
    }

    status = Scheduler::bootstrap("kernel", "boot");
    if (status != StatusOK) {
        board::Serial::puts("[kernel] scheduler bootstrap failed\n");
        halt();
    }

    board::Serial::puts("[kernel] scheduler ready\n");

    status = board::Platform::init_devices();
    if (status != StatusOK) {
        halt();
    }

    board::Serial::puts("[kernel] devices ready\n");

    status = board::Platform::init_timer();
    if ((status != StatusOK) && (status != StatusAlreadyExists)) {
        halt();
    }

    status = initialize_vfs_namespace();
    if (status != StatusOK) {
        halt();
    }

    {
        U64 frequency = arch::Arch::counter_frequency();
        U64 timeout_ticks = (frequency == 0ULL) ? 1ULL : (frequency / 20ULL);
        U64 start_counter = arch::Arch::counter_value();

        while ((Scheduler::tick_count() == 0ULL) && ((arch::Arch::counter_value() - start_counter) < timeout_ticks)) {
            // The timer is polled during early bring-up until the board interrupt controller is wired.
            Scheduler::poll();
        }

        if (Scheduler::tick_count() == 0ULL) {
            halt();
        }
    }

    board::Serial::puts("[kernel] timer tick ready\n");

    status = run_optional_boot_test("process smoke", g_kernel_boot_test_config.process, &run_process_manager_smoke_test, "[kernel] process manager ready\n");
    if (status != StatusOK) {
        halt_with_status("process smoke", status);
    }

    status = run_optional_boot_test("thread smoke", g_kernel_boot_test_config.threads, &run_thread_manager_smoke_test, "[kernel] thread manager ready\n");
    if (status != StatusOK) {
        halt_with_status("thread smoke", status);
    }

    status = run_optional_boot_test("kernel event smoke", g_kernel_boot_test_config.kernel_event, &run_kernel_event_smoke_test, "[kernel] kernel event ready\n");
    if (status != StatusOK) {
        halt_with_status("kernel event smoke", status);
    }

    status = run_optional_boot_test("shared memory smoke", g_kernel_boot_test_config.shared_memory, &run_shared_memory_smoke_test, "[kernel] shared memory ready\n");
    if (status != StatusOK) {
        halt_with_status("shared memory smoke", status);
    }

    status = run_optional_boot_test("scheduler smoke", g_kernel_boot_test_config.scheduler, &run_scheduler_boot_test, "[kernel] scheduler suite ready\n");
    if (status != StatusOK) {
        halt_with_status("scheduler smoke", status);
    }

    status = run_optional_boot_test("platform smoke", g_kernel_boot_test_config.platform, &run_platform_smoke_test, "[kernel] platform ready\n");
    if (status != StatusOK) {
        halt_with_status("platform smoke", status);
    }

    status = run_optional_boot_test("kernel input path", g_kernel_boot_test_config.kernel_input_path, &run_kernel_input_path_smoke_test, "[kernel] kernel input path ready\n");
    if (status != StatusOK) {
        halt_with_status("kernel input path", status);
    }

    status = run_optional_boot_test("kernel output path", g_kernel_boot_test_config.kernel_output_path, &run_kernel_output_path_smoke_test, "[kernel] kernel output path ready\n");
    if (status != StatusOK) {
        halt_with_status("kernel output path", status);
    }

    status = run_optional_boot_test("vfs namespace smoke", g_kernel_boot_test_config.vfs_namespace, &run_vfs_namespace_smoke_test, "[kernel] vfs namespace ready\n");
    if (status != StatusOK) {
        halt_with_status("vfs namespace smoke", status);
    }

    status = run_optional_boot_test("memory map smoke", g_kernel_boot_test_config.memory_map, &run_memory_map_smoke_test, "[kernel] memory map ready\n");
    if (status != StatusOK) {
        halt_with_status("memory map smoke", status);
    }

    status = run_optional_boot_test("physical memory smoke", g_kernel_boot_test_config.physical_memory, &run_physical_memory_smoke_test, "[kernel] physical memory ready\n");
    if (status != StatusOK) {
        halt_with_status("physical memory smoke", status);
    }

    console_device = DeviceManager::find_device("serial0");
    if (console_device == NULL) {
        halt();
    }

    const ConsoleSession console_session(console_device);

    if (g_kernel_boot_test_config.virtual_memory) {
        status = run_virtual_memory_smoke_test(&vm_test);
        if (status != StatusOK) {
            halt_with_status("virtual memory smoke", status);
        }

        vm_test_ran = true;
        board::Serial::puts("[kernel] virtual memory ready\n");
    }
    else {
        board::Serial::puts("[kernel] virtual memory smoke skipped\n");
    }

    console_session.write_text("HELLO WORLD\n");
    if (vm_test_ran) {
        console_session.write_text("VM TEST\n");
        console_session.write_text("  MMU enabled:                     ");
        console_session.write_bool(vm_test.mmu_enabled);
        console_session.write_text("\n  Kernel text high:              ");
        console_session.write_bool(vm_test.kernel_text_is_high);
        console_session.write_text("\n  Current address space active:  ");
        console_session.write_bool(vm_test.current_address_space_active);
        console_session.write_text("\n  Translation roundtrip:         ");
        console_session.write_bool(vm_test.translation_roundtrip);
        console_session.write_text("\n  Process address space root:    ");
        console_session.write_hex(vm_test.process_address_space_root);
        console_session.write_text("\n  Kernel address space root:     ");
        console_session.write_hex(vm_test.kernel_address_space_root);
        console_session.write_text("\n  kernel_entry VA:               ");
        console_session.write_hex(vm_test.kernel_text_address);
        console_session.write_text("\n  kernel_entry PA:               ");
        console_session.write_hex(vm_test.kernel_text_physical);
        console_session.write_text("\n");
    }

    status = start_userspace_core();
    if (status != StatusOK) {
        board::Serial::puts("[kernel] userspace core failed; attempting shell fallback\n");
        log_status("userspace core failure", status);
        status = start_userspace_shell();
        if (status != StatusOK) {
            board::Serial::puts("[kernel] userspace shell failed; falling back to kernel console\n");
            log_status("userspace shell failure", status);
        }
    }

    // test random memory access for automatic memory map
    char* test = (char*)0xFFFF000000001000;
    char* test2 = (char*)0xFFFF0000A0001000;
    test = (char*)"HELLO WORLD";
    test2 = (char*)"THIS IS THANG";

    console_session.run();
}
