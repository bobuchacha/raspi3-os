#include "heap.h"

#include "arch.h"
#include "debug-message.h"
#include "mm.h"
#include "mm/physical.h"

/*
 * Heap churn can produce a very large amount of allocator trace traffic during
 * boot stress and userspace launch. Keep those logs on the shared runtime
 * debug path so investigators can narrow output by zone or source file without
 * recompiling the allocator.
 */
#define HEAP_KDEBUG(fmt, ...) KDEBUG(KZONE_MEMORY, fmt, ##__VA_ARGS__)

namespace {

    /*
     * Keep the allocator metadata naturally aligned so every returned pointer
     * can satisfy the common kernel object alignment requirements without
     * needing per-call slow paths.
     */
    inline constexpr Size HeapMinimumAlignment = 16U;

    /*
     * Small allocations follow the Windows CE idea of routing hot sizes through
     * dedicated fixed-block regions rather than fragmenting the general free
     * list with process, thread, event, and tiny string allocations.
     */
    typedef struct HeapSmallBin {
        Size payload_bytes;
        const char* name;
    } HeapSmallBin;

    inline constexpr HeapSmallBin HeapSmallBins[] = {
        { 32U, "bin32" },
        { 64U, "bin64" },
        { 128U, "bin128" },
        { 256U, "bin256" },
        { 512U, "bin512" },
    };

    inline constexpr U16 HeapInvalidBinIndex = static_cast<U16>(0xFFFFU);
    inline constexpr U32 HeapSegmentMagic = 0x4853474dU;
    inline constexpr U32 HeapBlockAllocatedMagic = 0x48424c4bU;
    inline constexpr U32 HeapBlockFreedMagic = 0x48465245U;
    inline constexpr U64 HeapTailCanaryBase = 0xC0DEC0DE5A17F00DULL;
    inline constexpr Size HeapTailCanaryBytes = sizeof(U64);
    inline constexpr U8 HeapAllocatedPoison = 0xA5U;
    inline constexpr U8 HeapFreedPoison = 0xDDU;
    inline constexpr U32 HeapSegmentFlagInAvailableList = 1U << 0;
    inline constexpr bool HeapGrowthPreferBlockMapping = false;
    inline constexpr Size HeapGrowthUnitBytes = HeapGrowthPreferBlockMapping ? mm::backend::L2BlockSize : mm::PageSize;

    typedef enum HeapSegmentKind : U16 {
        HeapSegmentFree = 1,
        HeapSegmentGeneral = 2,
        HeapSegmentSlab = 3,
    } HeapSegmentKind;

    typedef enum HeapBlockKind : U16 {
        HeapBlockGeneral = 1,
        HeapBlockSlab = 2,
    } HeapBlockKind;

    typedef struct HeapSegment HeapSegment;

    /*
     * Every live allocation carries a compact header directly in front of the
     * caller-visible payload so free/realloc can validate ownership, request
     * size, and overrun canaries without having to rediscover layout state.
     */
    typedef struct HeapBlockHeader {
        U32 magic;
        U16 kind;
        U16 bin_index;
        Size requested_size;
        HeapSegment* owner_segment;
        HeapBlockHeader* next_free;
    } HeapBlockHeader;

    /*
     * The heap is partitioned into contiguous segments that cover the complete
     * bootstrap arena. Free segments feed either the general allocator or the
     * fixed-size slab bins; allocated segments never overlap or leave gaps.
     */
    typedef struct HeapSegment {
        U32 magic;
        U16 kind;
        U16 bin_index;
        Size total_size;
        HeapSegment* next;
        HeapSegment* prev;
        HeapSegment* available_next;
        HeapSegment* available_prev;
        HeapBlockHeader* block_list;
        Size user_bytes;
        Size block_stride;
        U32 block_count;
        U32 free_count;
        U32 allocation_count;
        U32 flags;
        /*
         * Keep the segment header size aligned to HeapMinimumAlignment.
         *
         * General aligned allocations derive a shifted segment start from the
         * combined segment-header and block-header footprint. If this header is
         * not itself a multiple of HeapMinimumAlignment, the allocator can only
         * satisfy larger payload alignments by creating a free prefix whose
         * segment start or total size violates the segment-chain invariants.
         */
        U64 alignment_padding;
    } HeapSegment;

    /*
     * The general allocator computes one concrete layout for a free segment so
     * the search path and the commit path cannot disagree about where metadata,
     * padding, and payload boundaries should land.
     */
    typedef struct HeapAllocationLayout {
        bool valid;
        Uptr segment_start;
        Uptr block_header_start;
        Uptr payload_start;
        Size prefix_bytes;
        Size required_bytes;
    } HeapAllocationLayout;

    static_assert((sizeof(HeapBlockHeader) % HeapMinimumAlignment) == 0U,
        "heap block headers must stay naturally aligned");
    static_assert((sizeof(HeapSegment) % HeapMinimumAlignment) == 0U,
        "heap segment headers must stay naturally aligned");

    HeapSegment* g_heap_segment_list_head;
    HeapSegment* g_small_bin_available_heads[COUNT_OF(HeapSmallBins)];
    Uptr g_heap_start_address;
    Uptr g_heap_end_address;
    bool g_heap_initialized;

    int heap_small_bin_index_for_request(Size size, Size alignment);
    Size heap_slab_segment_bytes(U16 bin_index);

    /*
     * Check whether one alignment request is usable by the allocator.
     *
     * The heap rewrite relies on power-of-two alignment math everywhere. Reject
     * unsupported requests early so the allocator never builds ambiguous state.
     *
     * @param value Alignment candidate.
     * @return True when `value` is a non-zero power of two.
     */
    bool is_power_of_two(Size value) {
        return (value != 0U) && ((value & (value - 1U)) == 0U);
    }

    /*
     * Round one byte count up to the next boundary.
     *
     * All internal segment and block sizes are rounded to the allocator's
     * minimum alignment so neighboring segments remain exactly adjacent.
     *
     * @param value Input byte count.
     * @param alignment Required power-of-two alignment.
     * @return Aligned byte count.
     */
    Size align_up(Size value, Size alignment) {
        const Size mask = alignment - 1U;

        return (value + mask) & ~mask;
    }

    /*
     * Compute the minimum growth batch needed for one failed allocation.
     *
     * The allocator still grows in page-sized mapping units today, but this
     * helper keeps the unit behind one switch so the kernel can later move to
     * larger block-based growth without changing the retry path.
     *
     * @param size Requested payload bytes.
     * @param alignment Requested payload alignment.
     * @return Minimum bytes to append before retrying the allocation.
     */
    Size heap_growth_bytes_for_request(Size size, Size alignment) {
        Size minimum_bytes;

        if ((heap_small_bin_index_for_request(size, alignment) >= 0) && (alignment <= HeapMinimumAlignment)) {
            minimum_bytes = heap_slab_segment_bytes(0U);
        }
        else {
            minimum_bytes = align_up(
                sizeof(HeapSegment) + sizeof(HeapBlockHeader) + size + alignment + HeapTailCanaryBytes,
                HeapMinimumAlignment);
        }

        if (minimum_bytes < HeapGrowthUnitBytes) {
            minimum_bytes = HeapGrowthUnitBytes;
        }

        return align_up(minimum_bytes, HeapGrowthUnitBytes);
    }

    /*
     * Round one address up to the next boundary.
     *
     * General allocations use this helper to place the user payload at the
     * caller-requested alignment while keeping the metadata immediately before
     * it for robust free/realloc validation.
     *
     * @param value Input address.
     * @param alignment Required power-of-two alignment.
     * @return Aligned address.
     */
    Uptr align_up_address(Uptr value, Size alignment) {
        const Uptr mask = static_cast<Uptr>(alignment - 1U);

        return (value + mask) & ~mask;
    }

    /*
     * Round one address down to the previous aligned boundary.
     *
     * Tail-biased large allocations need the latest possible aligned payload
     * start so they can preserve the biggest contiguous low-address free span
     * for future 2 MiB module and executable backing allocations.
     *
     * @param value Input address.
     * @param alignment Required power-of-two alignment.
     * @return Aligned address not greater than `value`.
     */
    Uptr align_down_address(Uptr value, Size alignment) {
        const Uptr mask = static_cast<Uptr>(alignment - 1U);

        return value & ~mask;
    }

    /*
     * Fill one byte range with a debug pattern.
     *
     * The CE heap invests heavily in poison patterns because silent stale-data
     * reuse is much harder to diagnose than a recognizable repeated byte.
     *
     * @param start Destination buffer.
     * @param value Byte pattern.
     * @param length Number of bytes to update.
     * @return Nothing.
     */
    void fill_bytes(void* start, U8 value, Size length) {
        U8* bytes = reinterpret_cast<U8*>(start);

        for (Size index = 0; index < length; ++index) {
            bytes[index] = value;
        }
    }

    /*
     * Convert one segment kind into a readable debug label.
     *
     * Detailed corruption logs are only useful when the allocator can print the
     * current role of the segment that failed validation.
     *
     * @param kind Stored segment kind value.
     * @return Static text label for logs.
     */
    const char* heap_segment_kind_name(U16 kind) {
        switch (kind) {
        case HeapSegmentFree:
            return "free";
        case HeapSegmentGeneral:
            return "general";
        case HeapSegmentSlab:
            return "slab";
        default:
            return "unknown";
        }
    }

    /*
     * Convert one block kind into a readable debug label.
     *
     * Distinguishing slab blocks from general blocks helps isolate whether one
     * bug came from the fixed-size fast path or the variable-size allocator.
     *
     * @param kind Stored block kind value.
     * @return Static text label for logs.
     */
    const char* heap_block_kind_name(U16 kind) {
        switch (kind) {
        case HeapBlockGeneral:
            return "general";
        case HeapBlockSlab:
            return "slab";
        default:
            return "unknown";
        }
    }

    /*
     * Return the minimum size worth keeping as a standalone free segment.
     *
     * Splitting off tiny unusable fragments is one of the easiest ways to make
     * a linked-list heap self-destruct over time. The allocator therefore only
     * creates a remainder when it can still carry a segment header plus some
     * aligned payload.
     *
     * @return Smallest legal free-segment size in bytes.
     */
    Size minimum_free_segment_size(void) {
        return align_up(sizeof(HeapSegment) + HeapMinimumAlignment, HeapMinimumAlignment);
    }

    /*
     * Check whether one address range lies completely inside the heap arena.
     *
     * Corruption handling should fail before dereferencing suspicious metadata,
     * so every segment and block validation starts with this range guard.
     *
     * @param pointer Range start.
     * @param bytes Number of bytes that must fit.
     * @return True when the full range lies inside the bootstrap arena.
     */
    bool heap_range_valid(const void* pointer, Size bytes) {
        const Uptr address = reinterpret_cast<Uptr>(pointer);

        if ((pointer == NULL) || (bytes == 0U) || (g_heap_start_address == 0U) || (g_heap_end_address <= g_heap_start_address)) {
            return false;
        }
        if (address < g_heap_start_address) {
            return false;
        }
        if (address > g_heap_end_address) {
            return false;
        }

        return bytes <= static_cast<Size>(g_heap_end_address - address);
    }

    /*
     * Return the exclusive end address of one segment.
     *
     * Many invariants are easier to express as address ranges than as sizes,
     * especially when checking adjacency and payload capacity.
     *
     * @param segment Segment to inspect.
     * @return Exclusive end address, or zero when `segment` is NULL.
     */
    Uptr heap_segment_end_address(const HeapSegment* segment) {
        if (segment == NULL) {
            return 0U;
        }

        return reinterpret_cast<Uptr>(segment) + segment->total_size;
    }

    /*
     * Return the allocator-managed payload bytes inside one segment.
     *
     * Statistics and compaction decisions care about how many bytes remain
     * available after segment metadata, not about the raw segment footprint.
     *
     * @param segment Segment to inspect.
     * @return Bytes inside the segment after the segment header.
     */
    Size heap_segment_payload_bytes(const HeapSegment* segment) {
        if ((segment == NULL) || (segment->total_size <= sizeof(HeapSegment))) {
            return 0U;
        }

        return segment->total_size - sizeof(HeapSegment);
    }

    /*
     * Return the caller-visible pointer for one block header.
     *
     * All block types place the user payload immediately after the block header
     * so free/realloc can recover metadata with a single subtraction.
     *
     * @param header Allocation header.
     * @return Caller-visible payload pointer.
     */
    void* heap_block_payload_pointer(HeapBlockHeader* header) {
        if (header == NULL) {
            return NULL;
        }

        return reinterpret_cast<void*>(header + 1);
    }

    /*
     * Compute the 64-bit tail signature for one allocation.
     *
     * The signature mixes the header address, size, and allocation kind so a
     * stale pointer is unlikely to satisfy the check accidentally.
     *
     * @param header Allocation header.
     * @return Expected tail canary value.
     */
    U64 heap_expected_tail_canary(const HeapBlockHeader* header) {
        if (header == NULL) {
            return 0U;
        }

        return HeapTailCanaryBase
            ^ static_cast<U64>(reinterpret_cast<Uptr>(header))
            ^ static_cast<U64>(header->requested_size)
            ^ (static_cast<U64>(header->kind) << 48U)
            ^ (static_cast<U64>(header->bin_index) << 32U);
    }

    /*
     * Return the storage location of one block's tail canary.
     *
     * The canary lives immediately after the caller-requested bytes so even a
     * one-byte overrun gets caught on free or realloc validation.
     *
     * @param header Allocation header.
     * @return Pointer to the stored tail canary.
     */
    U64* heap_tail_canary_pointer(HeapBlockHeader* header) {
        U8* payload = reinterpret_cast<U8*>(heap_block_payload_pointer(header));

        if (payload == NULL) {
            return NULL;
        }

        return reinterpret_cast<U64*>(payload + header->requested_size);
    }

    /*
     * Store the tail canary for one live allocation.
     *
     * The allocator refreshes the canary after each successful allocation or
     * in-place realloc so the checker always validates against current size.
     *
     * @param header Allocation header.
     * @return Nothing.
     */
    void heap_write_tail_canary(HeapBlockHeader* header) {
        U64* canary_pointer = heap_tail_canary_pointer(header);

        if (canary_pointer != NULL) {
            *canary_pointer = heap_expected_tail_canary(header);
        }
    }

    /*
     * Validate the tail canary of one live allocation.
     *
     * Tail validation is the fastest way to distinguish metadata corruption
     * from a caller that simply wrote beyond the requested byte count.
     *
     * @param header Allocation header.
     * @return True when the stored canary still matches.
     */
    bool heap_tail_canary_valid(HeapBlockHeader* header) {
        U64* canary_pointer = heap_tail_canary_pointer(header);

        if (canary_pointer == NULL) {
            return false;
        }

        return *canary_pointer == heap_expected_tail_canary(header);
    }

    /*
     * Check that one segment pointer and its declared size stay inside the arena.
     *
     * The previous allocator often crashed long after corruption because it
     * trusted `next` links during the walk. This helper rejects impossible
     * segment descriptors before the caller dereferences them further.
     *
     * @param segment Segment candidate.
     * @return True when the pointer and size look structurally valid.
     */
    bool heap_segment_pointer_valid(const HeapSegment* segment) {
        if (!heap_range_valid(segment, sizeof(HeapSegment))) {
            return false;
        }
        if (segment->magic != HeapSegmentMagic) {
            return false;
        }
        if ((segment->total_size < minimum_free_segment_size()) || ((segment->total_size & (HeapMinimumAlignment - 1U)) != 0U)) {
            return false;
        }

        return heap_range_valid(segment, segment->total_size);
    }

    /*
     * Check whether one block header belongs to one specific segment.
     *
     * Header validation must verify both that the header lies inside the arena
     * and that it stays within the owning segment's boundaries.
     *
     * @param segment Expected owner segment.
     * @param header Block header candidate.
     * @return True when the header lies completely inside `segment`.
     */
    bool heap_block_header_inside_segment(const HeapSegment* segment, const HeapBlockHeader* header) {
        const Uptr header_address = reinterpret_cast<Uptr>(header);
        const Uptr segment_start = reinterpret_cast<Uptr>(segment);
        const Uptr segment_end = heap_segment_end_address(segment);

        if ((segment == NULL) || (header == NULL)) {
            return false;
        }
        if (!heap_range_valid(header, sizeof(HeapBlockHeader))) {
            return false;
        }

        return (header_address >= segment_start) && ((header_address + sizeof(HeapBlockHeader)) <= segment_end);
    }

    /*
     * Return the byte capacity available to the caller inside one general block.
     *
     * General allocations reserve the whole segment after the block header, so
     * the usable capacity depends on where alignment pushed the payload start.
     *
     * @param segment Owning general segment.
     * @param header Block header inside the segment.
     * @return Maximum caller-visible bytes before the tail canary.
     */
    Size heap_general_capacity_bytes(const HeapSegment* segment, const HeapBlockHeader* header) {
        const Uptr payload_start = reinterpret_cast<Uptr>(heap_block_payload_pointer(const_cast<HeapBlockHeader*>(header)));
        const Uptr segment_end = heap_segment_end_address(segment);

        if ((segment == NULL) || (header == NULL) || (segment_end <= payload_start) || ((segment_end - payload_start) < HeapTailCanaryBytes)) {
            return 0U;
        }

        return static_cast<Size>(segment_end - payload_start - HeapTailCanaryBytes);
    }

    /*
     * Return the slab bin index that can satisfy one small allocation request.
     *
     * The fixed-size fast path is intentionally conservative: requests that ask
     * for larger-than-default alignment fall back to the general allocator so
     * the slab layout stays simple and corruption-resistant.
     *
     * @param size Requested byte count.
     * @param alignment Requested payload alignment.
     * @return Matching bin index, or `-1` when the request should use the
     *         general allocator.
     */
    int heap_small_bin_index_for_request(Size size, Size alignment) {
        if ((size == 0U) || (alignment > HeapMinimumAlignment)) {
            return -1;
        }

        for (Size index = 0; index < COUNT_OF(HeapSmallBins); ++index) {
            if (size <= HeapSmallBins[index].payload_bytes) {
                return static_cast<int>(index);
            }
        }

        return -1;
    }

    /*
     * Return the total bytes reserved for one slab segment.
     *
     * CE uses fixed-size regions for small allocation classes. One page per slab
     * is enough here because even the largest class still yields multiple blocks
     * and keeps the reclaim path cheap when an entire slab becomes idle.
     *
     * @param bin_index Small-bin index.
     * @return Total bytes to carve from the general free list.
     */
    Size heap_slab_segment_bytes(U16 bin_index) {
        (void)bin_index;
        return align_up(mm::PageSize, HeapMinimumAlignment);
    }

    /*
     * Check whether one slab segment is currently linked into its availability list.
     *
     * Slab allocation should never scan the full segment chain. This flag makes
     * list maintenance explicit and helps the validator catch stale links.
     *
     * @param segment Slab segment.
     * @return True when the segment is in the per-bin available list.
     */
    bool heap_segment_in_available_list(const HeapSegment* segment) {
        return (segment != NULL) && ((segment->flags & HeapSegmentFlagInAvailableList) != 0U);
    }

    /*
     * Insert one slab segment at the head of its per-bin available list.
     *
     * Keeping a direct free-segment head per bin gives the WinCE-style hot path:
     * repeated small allocations avoid a general free-list walk entirely.
     *
     * @param segment Slab segment with at least one free block.
     * @return Nothing.
     */
    void heap_small_bin_insert_available(HeapSegment* segment) {
        HeapSegment* head;

        if ((segment == NULL) || (segment->kind != HeapSegmentSlab) || (segment->bin_index >= COUNT_OF(HeapSmallBins)) || (segment->free_count == 0U) || heap_segment_in_available_list(segment)) {
            return;
        }

        head = g_small_bin_available_heads[segment->bin_index];
        segment->available_prev = NULL;
        segment->available_next = head;
        if (head != NULL) {
            head->available_prev = segment;
        }
        g_small_bin_available_heads[segment->bin_index] = segment;
        segment->flags |= HeapSegmentFlagInAvailableList;
    }

    /*
     * Remove one slab segment from its per-bin available list.
     *
     * Slabs leave the list when they become full or when they are being
     * reclaimed back into the general free-segment chain.
     *
     * @param segment Slab segment to unlink.
     * @return Nothing.
     */
    void heap_small_bin_remove_available(HeapSegment* segment) {
        if ((segment == NULL) || (segment->kind != HeapSegmentSlab) || (segment->bin_index >= COUNT_OF(HeapSmallBins)) || !heap_segment_in_available_list(segment)) {
            return;
        }

        if (segment->available_prev != NULL) {
            segment->available_prev->available_next = segment->available_next;
        }
        else {
            g_small_bin_available_heads[segment->bin_index] = segment->available_next;
        }
        if (segment->available_next != NULL) {
            segment->available_next->available_prev = segment->available_prev;
        }

        segment->available_prev = NULL;
        segment->available_next = NULL;
        segment->flags &= ~HeapSegmentFlagInAvailableList;
    }

    /*
     * Reset one segment header to the free-segment state.
     *
     * Freeing and coalescing should leave no stale slab/general bookkeeping in
     * the surviving header, otherwise later validation becomes ambiguous.
     *
     * @param segment Segment header to update.
     * @param total_size New total segment size.
     * @return Nothing.
     */
    void heap_mark_segment_free(HeapSegment* segment, Size total_size) {
        HeapSegment* next;
        HeapSegment* prev;

        if (segment == NULL) {
            return;
        }

        next = segment->next;
        prev = segment->prev;
        memzero(segment, sizeof(*segment));
        segment->magic = HeapSegmentMagic;
        segment->kind = HeapSegmentFree;
        segment->bin_index = HeapInvalidBinIndex;
        segment->total_size = total_size;
        segment->next = next;
        segment->prev = prev;
    }

    /*
     * Reset one segment header to a clean allocated state before the caller
     * fills in general or slab-specific metadata.
     *
     * Reinitializing the full header prevents state leakage across segment type
     * conversions, which is especially important when slabs are reclaimed.
     *
     * @param segment Segment header to update.
     * @param kind Allocated segment kind.
     * @param bin_index Bin index for slab segments or `HeapInvalidBinIndex`.
     * @return Nothing.
     */
    void heap_mark_segment_allocated(HeapSegment* segment, U16 kind, U16 bin_index) {
        HeapSegment* next;
        HeapSegment* prev;
        const Size total_size = (segment != NULL) ? segment->total_size : 0U;

        if (segment == NULL) {
            return;
        }

        next = segment->next;
        prev = segment->prev;
        memzero(segment, sizeof(*segment));
        segment->magic = HeapSegmentMagic;
        segment->kind = kind;
        segment->bin_index = bin_index;
        segment->total_size = total_size;
        segment->next = next;
        segment->prev = prev;
    }

    /*
     * Split one free segment so the leading bytes remain available as a free prefix.
     *
     * General allocations use this only when the aligned payload location leaves
     * enough room in front to create a useful free segment.
     *
     * @param segment Free segment being split.
     * @param prefix_bytes Bytes to keep in the original prefix segment.
     * @return Pointer to the suffix segment that starts after the prefix.
     */
    HeapSegment* heap_split_segment_prefix(HeapSegment* segment, Size prefix_bytes) {
        HeapSegment* suffix;
        HeapSegment* next;
        const Size original_size = (segment != NULL) ? segment->total_size : 0U;

        if ((segment == NULL) || (segment->kind != HeapSegmentFree) || (prefix_bytes < minimum_free_segment_size()) || (prefix_bytes >= original_size)) {
            return segment;
        }

        suffix = reinterpret_cast<HeapSegment*>(reinterpret_cast<U8*>(segment) + prefix_bytes);
        next = segment->next;

        heap_mark_segment_free(segment, prefix_bytes);
        heap_mark_segment_free(suffix, original_size - prefix_bytes);

        segment->next = suffix;
        suffix->prev = segment;
        suffix->next = next;
        if (next != NULL) {
            next->prev = suffix;
        }

        HEAP_KDEBUG(
            "[heap] split prefix segment=%llx prefix=%llu suffix=%llx suffix_bytes=%llu\n",
            static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
            static_cast<unsigned long long>(prefix_bytes),
            static_cast<unsigned long long>(reinterpret_cast<Uptr>(suffix)),
            static_cast<unsigned long long>(suffix->total_size));
        return suffix;
    }

    /*
     * Split one segment so the trailing bytes remain available as a free suffix.
     *
     * Both allocation and shrink-in-place paths use this helper to return the
     * excess tail to the general free list without disturbing the live segment.
     *
     * @param segment Segment that keeps the front portion.
     * @param leading_bytes Bytes to retain in `segment`.
     * @return Nothing.
     */
    void heap_split_segment_tail(HeapSegment* segment, Size leading_bytes) {
        HeapSegment* remainder;
        HeapSegment* next;
        const Size original_size = (segment != NULL) ? segment->total_size : 0U;
        const Size remainder_bytes = (original_size > leading_bytes) ? (original_size - leading_bytes) : 0U;

        if ((segment == NULL) || (leading_bytes >= original_size) || (remainder_bytes < minimum_free_segment_size())) {
            return;
        }

        remainder = reinterpret_cast<HeapSegment*>(reinterpret_cast<U8*>(segment) + leading_bytes);
        next = segment->next;

        heap_mark_segment_free(remainder, remainder_bytes);
        remainder->prev = segment;
        remainder->next = next;
        if (next != NULL) {
            next->prev = remainder;
        }

        segment->total_size = leading_bytes;
        segment->next = remainder;

        HEAP_KDEBUG(
            "[heap] split tail segment=%llx kept=%llu remainder=%llx remainder_bytes=%llu\n",
            static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
            static_cast<unsigned long long>(leading_bytes),
            static_cast<unsigned long long>(reinterpret_cast<Uptr>(remainder)),
            static_cast<unsigned long long>(remainder->total_size));
    }

    /*
     * Merge one free segment with adjacent free neighbors and return the head.
     *
     * Coalescing immediately on free keeps the general allocator from slowly
     * degenerating into tiny fragments and also enforces the validator rule
     * that no two adjacent free segments may remain separate.
     *
     * @param segment Free segment that just became available.
     * @return Head of the merged free extent.
     */
    HeapSegment* heap_coalesce_free_segment(HeapSegment* segment) {
        if ((segment == NULL) || (segment->kind != HeapSegmentFree)) {
            return segment;
        }

        while ((segment->prev != NULL) && (segment->prev->kind == HeapSegmentFree)) {
            HeapSegment* left = segment->prev;
            HeapSegment* next = segment->next;

            left->total_size += segment->total_size;
            left->next = next;
            if (next != NULL) {
                next->prev = left;
            }
            segment = left;
        }

        while ((segment->next != NULL) && (segment->next->kind == HeapSegmentFree)) {
            HeapSegment* right = segment->next;
            HeapSegment* next = right->next;

            segment->total_size += right->total_size;
            segment->next = next;
            if (next != NULL) {
                next->prev = segment;
            }
        }

        HEAP_KDEBUG(
            "[heap] coalesced free segment=%llx total=%llu\n",
            static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
            static_cast<unsigned long long>(segment->total_size));
        return segment;
    }

    /*
     * Return the current tail segment of the arena chain.
     *
     * Heap growth always appends at the arena tail so the existing segment
     * adjacency invariants remain valid after new pages are mapped in.
     *
     * @return Tail segment, or NULL when the arena is not initialized.
     */
    HeapSegment* heap_tail_segment_locked(void) {
        HeapSegment* tail = g_heap_segment_list_head;

        while ((tail != NULL) && (tail->next != NULL)) {
            tail = tail->next;
        }

        return tail;
    }

    /*
     * Append one freshly reserved mapped extent to the heap arena.
     *
     * The physical manager hands the heap a low-address contiguous run that is
     * already visible through the kernel direct map, so the allocator only
     * needs to extend the segment chain over the new bytes.
     *
     * @param growth_bytes Bytes to append to the arena.
     * @return StatusOK when the heap tail was extended successfully.
     */
    Status heap_append_growth_locked(Size growth_bytes) {
        HeapSegment* tail;
        const unsigned int page_count = static_cast<unsigned int>(growth_bytes / mm::PageSize);
        const PhysAddr growth_phys = mm::PhysicalMemory::reserve_contiguous_pages(page_count);
        const Uptr expected_address = g_heap_end_address;
        const Uptr growth_address = static_cast<Uptr>(mm::MemoryManager::physical_to_kernel(growth_phys));

        if ((growth_bytes == 0U) || ((growth_bytes % mm::PageSize) != 0U)) {
            return StatusInvalidArgument;
        }
        if (growth_phys == 0U) {
            return StatusNoMemory;
        }
        if (growth_address != expected_address) {
            KERROR(
                "[heap] growth contiguity failed expected=%llx actual=%llx phys=%llx bytes=%llu\n",
                static_cast<unsigned long long>(expected_address),
                static_cast<unsigned long long>(growth_address),
                static_cast<unsigned long long>(growth_phys),
                static_cast<unsigned long long>(growth_bytes));
            mm::PhysicalMemory::release_contiguous_pages(growth_phys, page_count);
            return StatusFault;
        }

        tail = heap_tail_segment_locked();
        if (tail == NULL) {
            mm::PhysicalMemory::release_contiguous_pages(growth_phys, page_count);
            return StatusFault;
        }

        g_heap_end_address += growth_bytes;
        if (tail->kind == HeapSegmentFree) {
            tail->total_size += growth_bytes;
        }
        else {
            HeapSegment* appended = reinterpret_cast<HeapSegment*>(growth_address);

            heap_mark_segment_free(appended, growth_bytes);
            appended->prev = tail;
            appended->next = NULL;
            tail->next = appended;
        }

        HEAP_KDEBUG(
            "[heap] grew arena base=%llx end=%llx bytes=%llu unit=%llu\n",
            static_cast<unsigned long long>(g_heap_start_address),
            static_cast<unsigned long long>(g_heap_end_address),
            static_cast<unsigned long long>(growth_bytes),
            static_cast<unsigned long long>(HeapGrowthUnitBytes));
        return StatusOK;
    }

    /*
     * Grow the heap enough to retry one failed allocation.
     *
     * @param size Requested payload bytes.
     * @param alignment Requested alignment.
     * @return StatusOK when new arena bytes were added.
     */
    Status heap_grow_locked(Size size, Size alignment) {
        return heap_append_growth_locked(heap_growth_bytes_for_request(size, alignment));
    }

    /*
     * Dump a compact view of the current segment chain.
     *
     * Corruption logs should print the first few segments immediately so the
     * first broken link can be correlated with the allocator state on failure.
     *
     * @param reason Diagnostic label for the dump.
     * @param limit Maximum number of segments to print.
     * @return Nothing.
     */
    void heap_debug_dump_segments_locked(const char* reason, Size limit) {
        HeapSegment* segment = g_heap_segment_list_head;
        Size index = 0U;

        KERROR(
            "[heap] dump reason=%s head=%llx arena=[%llx,%llx)\n",
            reason != NULL ? reason : "<none>",
            static_cast<unsigned long long>(reinterpret_cast<Uptr>(g_heap_segment_list_head)),
            static_cast<unsigned long long>(g_heap_start_address),
            static_cast<unsigned long long>(g_heap_end_address));

        while ((segment != NULL) && (index < limit)) {
            KERROR(
                "[heap]   seg[%llu] addr=%llx kind=%s size=%llu prev=%llx next=%llx user=%llu stride=%llu blocks=%u free=%u alloc=%u block=%llx\n",
                static_cast<unsigned long long>(index),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                heap_segment_kind_name(segment->kind),
                static_cast<unsigned long long>(segment->total_size),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment->prev)),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment->next)),
                static_cast<unsigned long long>(segment->user_bytes),
                static_cast<unsigned long long>(segment->block_stride),
                segment->block_count,
                segment->free_count,
                segment->allocation_count,
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment->block_list)));
            segment = segment->next;
            ++index;
        }
    }

    /*
     * Report one allocator corruption and include a short segment dump.
     *
     * Centralizing the failure path keeps the logs consistent and ensures every
     * hard failure prints the same level of context.
     *
     * @param reason Diagnostic label.
     * @return Nothing.
     */
    void heap_report_corruption_locked(const char* reason) {
        heap_debug_dump_segments_locked(reason, 24U);
    }

    /*
     * Validate one general-allocation segment.
     *
     * General allocations are represented by exactly one live block header per
     * segment. Verifying that header catches stale owner pointers, truncated
     * sizes, and writes beyond the requested payload.
     *
     * @param segment Segment to validate.
     * @param reason Diagnostic label.
     * @return True when the segment remains internally consistent.
     */
    bool heap_validate_general_segment_locked(HeapSegment* segment, const char* reason) {
        HeapBlockHeader* header = (segment != NULL) ? segment->block_list : NULL;
        const Uptr segment_end = heap_segment_end_address(segment);
        const Uptr payload_start = reinterpret_cast<Uptr>(heap_block_payload_pointer(header));

        if (segment == NULL) {
            return false;
        }
        if ((segment->allocation_count != 1U) || (segment->free_count != 0U) || (segment->block_count != 1U) || (header == NULL)) {
            KERROR(
                "[heap] validate general counters failed reason=%s segment=%llx blocks=%u free=%u alloc=%u block=%llx\n",
                reason != NULL ? reason : "<none>",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                segment->block_count,
                segment->free_count,
                segment->allocation_count,
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)));
            return false;
        }
        if (!heap_block_header_inside_segment(segment, header)) {
            KERROR(
                "[heap] validate general header range failed reason=%s segment=%llx header=%llx\n",
                reason != NULL ? reason : "<none>",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)));
            return false;
        }
        if ((header->magic != HeapBlockAllocatedMagic) || (header->kind != HeapBlockGeneral) || (header->owner_segment != segment)) {
            KERROR(
                "[heap] validate general header failed reason=%s segment=%llx header=%llx magic=%x kind=%s owner=%llx\n",
                reason != NULL ? reason : "<none>",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)),
                header->magic,
                heap_block_kind_name(header->kind),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(header->owner_segment)));
            return false;
        }
        if ((segment->user_bytes != header->requested_size) || (header->requested_size == 0U)) {
            KERROR(
                "[heap] validate general size failed reason=%s segment=%llx seg_user=%llu hdr_user=%llu\n",
                reason != NULL ? reason : "<none>",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                static_cast<unsigned long long>(segment->user_bytes),
                static_cast<unsigned long long>(header->requested_size));
            return false;
        }
        if ((payload_start <= reinterpret_cast<Uptr>(header)) || ((payload_start + header->requested_size + HeapTailCanaryBytes) > segment_end)) {
            KERROR(
                "[heap] validate general bounds failed reason=%s segment=%llx payload=%llx size=%llu end=%llx\n",
                reason != NULL ? reason : "<none>",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                static_cast<unsigned long long>(payload_start),
                static_cast<unsigned long long>(header->requested_size),
                static_cast<unsigned long long>(segment_end));
            return false;
        }
        if (!heap_tail_canary_valid(header)) {
            KERROR(
                "[heap] validate general canary failed reason=%s segment=%llx header=%llx payload=%llx size=%llu\n",
                reason != NULL ? reason : "<none>",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)),
                static_cast<unsigned long long>(payload_start),
                static_cast<unsigned long long>(header->requested_size));
            return false;
        }

        return true;
    }

    /*
     * Validate one slab segment and its internal free list.
     *
     * Slabs trade general-list fragmentation for internal per-bin metadata, so
     * the validator has to confirm both the block descriptors and the free-list
     * chain that feeds the small-allocation fast path.
     *
     * @param segment Segment to validate.
     * @param reason Diagnostic label.
     * @return True when the slab metadata remains internally consistent.
     */
    bool heap_validate_slab_segment_locked(HeapSegment* segment, const char* reason) {
        U64 free_seen[8] = {};
        const Uptr segment_start = reinterpret_cast<Uptr>(segment);
        const Uptr block_base = align_up_address(segment_start + sizeof(HeapSegment), HeapMinimumAlignment);
        const Uptr segment_end = heap_segment_end_address(segment);
        const Size block_bytes = segment->block_stride;
        U32 observed_allocations = 0U;
        U32 observed_frees = 0U;

        if ((segment == NULL) || (segment->bin_index >= COUNT_OF(HeapSmallBins)) || (segment->block_stride == 0U) || (segment->block_count == 0U)) {
            KERROR(
                "[heap] validate slab header failed reason=%s segment=%llx bin=%u stride=%llu blocks=%u\n",
                reason != NULL ? reason : "<none>",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                segment != NULL ? segment->bin_index : 0U,
                static_cast<unsigned long long>(segment != NULL ? segment->block_stride : 0U),
                segment != NULL ? segment->block_count : 0U);
            return false;
        }
        if (segment->user_bytes != HeapSmallBins[segment->bin_index].payload_bytes) {
            KERROR(
                "[heap] validate slab bin size failed reason=%s segment=%llx user=%llu expected=%llu\n",
                reason != NULL ? reason : "<none>",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                static_cast<unsigned long long>(segment->user_bytes),
                static_cast<unsigned long long>(HeapSmallBins[segment->bin_index].payload_bytes));
            return false;
        }
        if ((segment->free_count + segment->allocation_count) != segment->block_count) {
            KERROR(
                "[heap] validate slab counters failed reason=%s segment=%llx blocks=%u free=%u alloc=%u\n",
                reason != NULL ? reason : "<none>",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                segment->block_count,
                segment->free_count,
                segment->allocation_count);
            return false;
        }
        if ((block_base >= segment_end) || ((block_base + (static_cast<Uptr>(segment->block_count) * block_bytes)) > segment_end)) {
            KERROR(
                "[heap] validate slab range failed reason=%s segment=%llx block_base=%llx end=%llx stride=%llu blocks=%u\n",
                reason != NULL ? reason : "<none>",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                static_cast<unsigned long long>(block_base),
                static_cast<unsigned long long>(segment_end),
                static_cast<unsigned long long>(block_bytes),
                segment->block_count);
            return false;
        }
        if (segment->block_count > (COUNT_OF(free_seen) * 64U)) {
            KERROR(
                "[heap] validate slab bitmap too small reason=%s segment=%llx blocks=%u\n",
                reason != NULL ? reason : "<none>",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                segment->block_count);
            return false;
        }

        for (U32 index = 0U; index < segment->block_count; ++index) {
            HeapBlockHeader* header = reinterpret_cast<HeapBlockHeader*>(block_base + (static_cast<Uptr>(index) * block_bytes));

            if (!heap_block_header_inside_segment(segment, header)) {
                KERROR(
                    "[heap] validate slab block range failed reason=%s segment=%llx header=%llx index=%u\n",
                    reason != NULL ? reason : "<none>",
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)),
                    index);
                return false;
            }
            if ((header->kind != HeapBlockSlab) || (header->bin_index != segment->bin_index) || (header->owner_segment != segment)) {
                KERROR(
                    "[heap] validate slab block owner failed reason=%s segment=%llx header=%llx kind=%s bin=%u owner=%llx\n",
                    reason != NULL ? reason : "<none>",
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)),
                    heap_block_kind_name(header->kind),
                    header->bin_index,
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(header->owner_segment)));
                return false;
            }

            if (header->magic == HeapBlockAllocatedMagic) {
                ++observed_allocations;
                if ((header->requested_size == 0U) || (header->requested_size > segment->user_bytes) || !heap_tail_canary_valid(header)) {
                    KERROR(
                        "[heap] validate slab allocated block failed reason=%s segment=%llx header=%llx size=%llu\n",
                        reason != NULL ? reason : "<none>",
                        static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                        static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)),
                        static_cast<unsigned long long>(header->requested_size));
                    return false;
                }
            }
            else if (header->magic == HeapBlockFreedMagic) {
                ++observed_frees;
            }
            else {
                KERROR(
                    "[heap] validate slab magic failed reason=%s segment=%llx header=%llx magic=%x\n",
                    reason != NULL ? reason : "<none>",
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)),
                    header->magic);
                return false;
            }
        }

        if ((observed_allocations != segment->allocation_count) || (observed_frees != segment->free_count)) {
            KERROR(
                "[heap] validate slab observed counts failed reason=%s segment=%llx seen_alloc=%u seen_free=%u alloc=%u free=%u\n",
                reason != NULL ? reason : "<none>",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                observed_allocations,
                observed_frees,
                segment->allocation_count,
                segment->free_count);
            return false;
        }

        {
            HeapBlockHeader* free_node = segment->block_list;
            U32 walked = 0U;

            while (free_node != NULL) {
                const Uptr address = reinterpret_cast<Uptr>(free_node);
                const Uptr relative = address - block_base;
                const U32 index = static_cast<U32>(relative / block_bytes);
                const U32 word_index = index / 64U;
                const U32 bit_index = index % 64U;

                if ((address < block_base) || (address >= segment_end) || ((relative % block_bytes) != 0U) || (index >= segment->block_count)) {
                    KERROR(
                        "[heap] validate slab free-list range failed reason=%s segment=%llx node=%llx\n",
                        reason != NULL ? reason : "<none>",
                        static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                        static_cast<unsigned long long>(address));
                    return false;
                }
                if ((free_node->magic != HeapBlockFreedMagic) || (free_node->owner_segment != segment)) {
                    KERROR(
                        "[heap] validate slab free-list node failed reason=%s segment=%llx node=%llx magic=%x owner=%llx\n",
                        reason != NULL ? reason : "<none>",
                        static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                        static_cast<unsigned long long>(address),
                        free_node->magic,
                        static_cast<unsigned long long>(reinterpret_cast<Uptr>(free_node->owner_segment)));
                    return false;
                }
                if ((free_seen[word_index] & (1ULL << bit_index)) != 0U) {
                    KERROR(
                        "[heap] validate slab free-list duplicate failed reason=%s segment=%llx node=%llx index=%u\n",
                        reason != NULL ? reason : "<none>",
                        static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                        static_cast<unsigned long long>(address),
                        index);
                    return false;
                }

                free_seen[word_index] |= (1ULL << bit_index);
                free_node = free_node->next_free;
                ++walked;
                if (walked > segment->block_count) {
                    KERROR(
                        "[heap] validate slab free-list cycle failed reason=%s segment=%llx walked=%u blocks=%u\n",
                        reason != NULL ? reason : "<none>",
                        static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                        walked,
                        segment->block_count);
                    return false;
                }
            }

            if (walked != segment->free_count) {
                KERROR(
                    "[heap] validate slab free-list count failed reason=%s segment=%llx walked=%u free=%u\n",
                    reason != NULL ? reason : "<none>",
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                    walked,
                    segment->free_count);
                return false;
            }
        }

        return true;
    }

    /*
     * Validate the full segment chain without taking any allocator action.
     *
     * This checker enforces the core heap invariants: contiguous coverage of the
     * arena, correct back-links, no adjacent free segments, and valid metadata
     * for both general and slab allocation paths.
     *
     * @param reason Diagnostic label.
     * @return True when the heap structure remains internally consistent.
     */
    bool heap_validate_locked(const char* reason) {
        HeapSegment* segment;
        HeapSegment* previous_segment = NULL;
        Uptr expected_address;

        if (!g_heap_initialized || (g_heap_segment_list_head == NULL)) {
            return false;
        }

        segment = g_heap_segment_list_head;
        expected_address = g_heap_start_address;
        while (segment != NULL) {
            if (!heap_segment_pointer_valid(segment)) {
                KERROR(
                    "[heap] validate segment pointer failed reason=%s segment=%llx expected=%llx\n",
                    reason != NULL ? reason : "<none>",
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                    static_cast<unsigned long long>(expected_address));
                heap_report_corruption_locked(reason);
                return false;
            }
            if (reinterpret_cast<Uptr>(segment) != expected_address) {
                KERROR(
                    "[heap] validate adjacency failed reason=%s segment=%llx expected=%llx\n",
                    reason != NULL ? reason : "<none>",
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                    static_cast<unsigned long long>(expected_address));
                heap_report_corruption_locked(reason);
                return false;
            }
            if (segment->prev != previous_segment) {
                KERROR(
                    "[heap] validate backlink failed reason=%s segment=%llx prev=%llx expected=%llx\n",
                    reason != NULL ? reason : "<none>",
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment->prev)),
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(previous_segment)));
                heap_report_corruption_locked(reason);
                return false;
            }
            if ((segment->next != NULL) && (reinterpret_cast<Uptr>(segment->next) != heap_segment_end_address(segment))) {
                KERROR(
                    "[heap] validate next adjacency failed reason=%s segment=%llx next=%llx expected=%llx\n",
                    reason != NULL ? reason : "<none>",
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment->next)),
                    static_cast<unsigned long long>(heap_segment_end_address(segment)));
                heap_report_corruption_locked(reason);
                return false;
            }
            if ((segment->kind == HeapSegmentFree) && (segment->next != NULL) && (segment->next->kind == HeapSegmentFree)) {
                KERROR(
                    "[heap] validate adjacent free failed reason=%s segment=%llx next=%llx\n",
                    reason != NULL ? reason : "<none>",
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment->next)));
                heap_report_corruption_locked(reason);
                return false;
            }

            if (segment->kind == HeapSegmentGeneral) {
                if (!heap_validate_general_segment_locked(segment, reason)) {
                    heap_report_corruption_locked(reason);
                    return false;
                }
            }
            else if (segment->kind == HeapSegmentSlab) {
                if (!heap_validate_slab_segment_locked(segment, reason)) {
                    heap_report_corruption_locked(reason);
                    return false;
                }
            }
            else if (segment->kind != HeapSegmentFree) {
                KERROR(
                    "[heap] validate unknown segment kind failed reason=%s segment=%llx kind=%u\n",
                    reason != NULL ? reason : "<none>",
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                    segment->kind);
                heap_report_corruption_locked(reason);
                return false;
            }

            expected_address = heap_segment_end_address(segment);
            previous_segment = segment;
            segment = segment->next;
        }

        if (expected_address != g_heap_end_address) {
            KERROR(
                "[heap] validate arena coverage failed reason=%s expected_end=%llx arena_end=%llx\n",
                reason != NULL ? reason : "<none>",
                static_cast<unsigned long long>(expected_address),
                static_cast<unsigned long long>(g_heap_end_address));
            heap_report_corruption_locked(reason);
            return false;
        }

        return true;
    }

    /*
     * Compute the aligned layout for one general allocation inside one free segment.
     *
     * The older allocator had separate search and commit logic, which allowed
     * alignment corner cases to corrupt the list. This helper produces the one
     * authoritative answer that both phases reuse.
     *
     * @param segment Free segment candidate.
     * @param size Requested payload bytes.
     * @param alignment Requested payload alignment.
     * @return Fully described allocation layout, or `valid == false` when the
     *         segment cannot satisfy the request.
     */
    HeapAllocationLayout heap_compute_general_layout(const HeapSegment* segment, Size size, Size alignment) {
        HeapAllocationLayout layout = {};
        const Uptr original_segment_start = reinterpret_cast<Uptr>(segment);
        const Uptr original_segment_end = heap_segment_end_address(segment);
        const Size metadata_bytes = sizeof(HeapSegment) + sizeof(HeapBlockHeader);
        const Uptr payload_without_prefix = align_up_address(original_segment_start + sizeof(HeapSegment) + sizeof(HeapBlockHeader), alignment);
        const Uptr shifted_segment_start = payload_without_prefix - sizeof(HeapSegment) - sizeof(HeapBlockHeader);
        Uptr allocation_segment_start = original_segment_start;
        Size prefix_bytes = 0U;

        if ((segment == NULL) || (segment->kind != HeapSegmentFree) || (size == 0U) || !is_power_of_two(alignment)) {
            return layout;
        }

        /*
         * Very large aligned allocations are the loader hot path for EXE and DLL
         * backing slots. Carving them from the tail keeps the surviving free
         * prefix contiguous, which avoids quickly destroying the next 2 MiB-
         * aligned hole after a burst of smaller heap traffic.
         */
        if ((alignment >= mm::backend::L2BlockSize)
            && (original_segment_end > original_segment_start)
            && ((original_segment_end - original_segment_start) >= (metadata_bytes + size + HeapTailCanaryBytes))) {
            const Uptr latest_payload_limit = original_segment_end - size - HeapTailCanaryBytes;
            const Uptr tail_payload_start = align_down_address(latest_payload_limit, alignment);

            if (tail_payload_start >= (original_segment_start + metadata_bytes)) {
                const Uptr tail_segment_start = tail_payload_start - metadata_bytes;
                const Size tail_prefix_bytes = static_cast<Size>(tail_segment_start - original_segment_start);

                if ((tail_segment_start >= original_segment_start)
                    && ((tail_prefix_bytes == 0U) || (tail_prefix_bytes >= minimum_free_segment_size()))) {
                    layout.segment_start = tail_segment_start;
                    layout.payload_start = align_up_address(layout.segment_start + metadata_bytes, alignment);
                    layout.block_header_start = layout.payload_start - sizeof(HeapBlockHeader);
                    layout.prefix_bytes = tail_prefix_bytes;
                    layout.required_bytes = static_cast<Size>(original_segment_end - tail_segment_start);

                    if ((layout.payload_start + size + HeapTailCanaryBytes) <= original_segment_end) {
                        layout.valid = true;
                        return layout;
                    }
                }
            }
        }

        if (shifted_segment_start > original_segment_start) {
            prefix_bytes = static_cast<Size>(shifted_segment_start - original_segment_start);
            if (prefix_bytes >= minimum_free_segment_size()) {
                allocation_segment_start = shifted_segment_start;
            }
            else {
                prefix_bytes = 0U;
            }
        }

        layout.segment_start = allocation_segment_start;
        layout.payload_start = align_up_address(layout.segment_start + sizeof(HeapSegment) + sizeof(HeapBlockHeader), alignment);
        layout.block_header_start = layout.payload_start - sizeof(HeapBlockHeader);
        layout.prefix_bytes = prefix_bytes;
        layout.required_bytes = align_up(
            static_cast<Size>((layout.payload_start + size + HeapTailCanaryBytes) - layout.segment_start),
            HeapMinimumAlignment);

        if ((layout.prefix_bytes + layout.required_bytes) > segment->total_size) {
            layout.valid = false;
            return layout;
        }

        layout.valid = true;
        return layout;
    }

    /*
     * Reserve one exact-size segment from the general free chain.
     *
     * Slab creation needs raw segment bytes rather than a normal allocation
     * header. This helper isolates one free extent without introducing any block
     * metadata so the caller can repurpose the segment as a slab region.
     *
     * @param total_bytes Exact bytes to carve from a free segment.
     * @param caller_hint Return address of the requester for diagnostics.
     * @return Isolated free segment on success, or NULL on failure.
     */
    HeapSegment* heap_reserve_segment(Size total_bytes, Uptr caller_hint) {
        HeapSegment* best_segment = NULL;
        Size best_waste = 0U;

        for (HeapSegment* segment = g_heap_segment_list_head; segment != NULL; segment = segment->next) {
            if (!heap_segment_pointer_valid(segment)) {
                KERROR(
                    "[heap] reserve corrupt segment head=%llx segment=%llx caller=%llx\n",
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(g_heap_segment_list_head)),
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                    static_cast<unsigned long long>(caller_hint));
                heap_report_corruption_locked("reserve-corrupt");
                return NULL;
            }
            if ((segment->kind != HeapSegmentFree) || (segment->total_size < total_bytes)) {
                continue;
            }

            if ((best_segment == NULL) || ((segment->total_size - total_bytes) < best_waste)) {
                best_segment = segment;
                best_waste = segment->total_size - total_bytes;
            }
        }

        if (best_segment == NULL) {
            return NULL;
        }

        heap_split_segment_tail(best_segment, total_bytes);
        return best_segment;
    }

    /*
     * Build the free-list metadata for one freshly reserved slab segment.
     *
     * Each slab segment contains only one size class, which makes small alloc
     * and free O(1) while isolating those objects from the general allocator.
     *
     * @param segment Reserved segment to convert.
     * @param bin_index Small-bin index.
     * @return True when the slab region is ready for allocation.
     */
    bool heap_initialize_slab_segment(HeapSegment* segment, U16 bin_index) {
        const Uptr segment_start = reinterpret_cast<Uptr>(segment);
        const Uptr block_base = align_up_address(segment_start + sizeof(HeapSegment), HeapMinimumAlignment);
        const Size stride = align_up(sizeof(HeapBlockHeader) + HeapSmallBins[bin_index].payload_bytes + HeapTailCanaryBytes, HeapMinimumAlignment);
        const Size available_bytes = static_cast<Size>(heap_segment_end_address(segment) - block_base);
        const U32 block_count = static_cast<U32>(available_bytes / stride);
        HeapBlockHeader* head = NULL;

        if ((segment == NULL) || (bin_index >= COUNT_OF(HeapSmallBins)) || (block_count == 0U)) {
            return false;
        }

        heap_mark_segment_allocated(segment, HeapSegmentSlab, bin_index);
        segment->user_bytes = HeapSmallBins[bin_index].payload_bytes;
        segment->block_stride = stride;
        segment->block_count = block_count;
        segment->free_count = block_count;
        segment->allocation_count = 0U;

        for (I32 index = static_cast<I32>(block_count) - 1; index >= 0; --index) {
            HeapBlockHeader* header = reinterpret_cast<HeapBlockHeader*>(block_base + (static_cast<Uptr>(index) * stride));

            memzero(header, sizeof(*header));
            header->magic = HeapBlockFreedMagic;
            header->kind = HeapBlockSlab;
            header->bin_index = bin_index;
            header->owner_segment = segment;
            header->next_free = head;
            head = header;
            fill_bytes(heap_block_payload_pointer(header), HeapFreedPoison, segment->user_bytes + HeapTailCanaryBytes);
        }

        segment->block_list = head;
        heap_small_bin_insert_available(segment);
        HEAP_KDEBUG(
            "[heap] slab init segment=%llx bin=%s size=%llu stride=%llu blocks=%u\n",
            static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
            HeapSmallBins[bin_index].name,
            static_cast<unsigned long long>(segment->total_size),
            static_cast<unsigned long long>(segment->block_stride),
            segment->block_count);
        return true;
    }

    /*
     * Create a new slab segment for one small-bin class.
     *
     * Slabs are created lazily so rarely used bins do not permanently consume
     * heap space until the workload actually asks for them.
     *
     * @param bin_index Small-bin index.
     * @param caller_hint Return address of the requester.
     * @return Ready slab segment, or NULL when reservation fails.
     */
    HeapSegment* heap_create_slab_segment(U16 bin_index, Uptr caller_hint) {
        HeapSegment* segment = heap_reserve_segment(heap_slab_segment_bytes(bin_index), caller_hint);

        if (segment == NULL) {
            KERROR(
                "[heap] slab create failed caller=%llx bin=%s bytes=%llu\n",
                static_cast<unsigned long long>(caller_hint),
                HeapSmallBins[bin_index].name,
                static_cast<unsigned long long>(heap_slab_segment_bytes(bin_index)));
            return NULL;
        }
        if (!heap_initialize_slab_segment(segment, bin_index)) {
            heap_mark_segment_free(segment, segment->total_size);
            (void)heap_coalesce_free_segment(segment);
            KERROR(
                "[heap] slab init failed caller=%llx bin=%s segment=%llx\n",
                static_cast<unsigned long long>(caller_hint),
                HeapSmallBins[bin_index].name,
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)));
            return NULL;
        }

        return segment;
    }

    /*
     * Snapshot heap usage counters.
     *
     * The counters intentionally reflect allocator-reserved bytes rather than
     * only caller-requested sizes so fragmentation and metadata pressure remain
     * visible to diagnostics.
     *
     * @param stats_out Destination statistics structure.
     * @return Nothing.
     */
    void heap_refresh_stats(HeapStats* stats_out) {
        HeapStats stats = {};

        for (HeapSegment* segment = g_heap_segment_list_head; segment != NULL; segment = segment->next) {
            if (segment->kind == HeapSegmentFree) {
                const Size bytes = heap_segment_payload_bytes(segment);

                stats.total_bytes += bytes;
                stats.free_bytes += bytes;
            }
            else if (segment->kind == HeapSegmentGeneral) {
                const Size bytes = heap_segment_payload_bytes(segment);

                stats.total_bytes += bytes;
                stats.used_bytes += bytes;
            }
            else if (segment->kind == HeapSegmentSlab) {
                const Size bytes = static_cast<Size>(segment->block_count) * segment->block_stride;

                stats.total_bytes += bytes;
                stats.used_bytes += static_cast<Size>(segment->allocation_count) * segment->block_stride;
                stats.free_bytes += static_cast<Size>(segment->free_count) * segment->block_stride;
            }
        }

        if (stats_out != NULL) {
            *stats_out = stats;
        }
    }

    /*
     * Validate one caller-supplied allocation pointer and recover its metadata.
     *
     * Free and realloc must reject malformed pointers cleanly, otherwise one bad
     * caller write can turn into list corruption inside the allocator itself.
     *
     * @param pointer Caller-supplied payload pointer.
     * @param reason Diagnostic label.
     * @param header_out Receives the recovered block header.
     * @param segment_out Receives the owning segment.
     * @return True when `pointer` still references one live allocation.
     */
    bool heap_lookup_live_allocation_locked(void* pointer, const char* reason, HeapBlockHeader** header_out, HeapSegment** segment_out) {
        HeapBlockHeader* header;
        HeapSegment* segment;

        if (header_out != NULL) {
            *header_out = NULL;
        }
        if (segment_out != NULL) {
            *segment_out = NULL;
        }
        if (pointer == NULL) {
            return false;
        }
        if (!heap_range_valid(pointer, 1U) || (reinterpret_cast<Uptr>(pointer) < (g_heap_start_address + sizeof(HeapBlockHeader)))) {
            KERROR(
                "[heap] %s pointer outside heap pointer=%llx\n",
                reason != NULL ? reason : "lookup",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(pointer)));
            return false;
        }

        header = reinterpret_cast<HeapBlockHeader*>(reinterpret_cast<U8*>(pointer) - sizeof(HeapBlockHeader));
        if (!heap_range_valid(header, sizeof(*header))) {
            KERROR(
                "[heap] %s header outside heap pointer=%llx header=%llx\n",
                reason != NULL ? reason : "lookup",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(pointer)),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)));
            return false;
        }
        if (header->magic != HeapBlockAllocatedMagic) {
            KERROR(
                "[heap] %s invalid block magic pointer=%llx header=%llx magic=%x\n",
                reason != NULL ? reason : "lookup",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(pointer)),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)),
                header->magic);
            return false;
        }

        segment = header->owner_segment;
        if (!heap_segment_pointer_valid(segment)) {
            KERROR(
                "[heap] %s invalid owner segment pointer=%llx header=%llx owner=%llx\n",
                reason != NULL ? reason : "lookup",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(pointer)),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)));
            heap_report_corruption_locked(reason);
            return false;
        }
        if (!heap_block_header_inside_segment(segment, header)) {
            KERROR(
                "[heap] %s header outside owner pointer=%llx header=%llx owner=%llx\n",
                reason != NULL ? reason : "lookup",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(pointer)),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)));
            heap_report_corruption_locked(reason);
            return false;
        }
        if ((header->kind == HeapBlockGeneral) && (segment->kind != HeapSegmentGeneral)) {
            KERROR(
                "[heap] %s block/segment kind mismatch pointer=%llx header_kind=%s segment_kind=%s segment=%llx\n",
                reason != NULL ? reason : "lookup",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(pointer)),
                heap_block_kind_name(header->kind),
                heap_segment_kind_name(segment->kind),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)));
            heap_report_corruption_locked(reason);
            return false;
        }
        if ((header->kind == HeapBlockSlab) && (segment->kind != HeapSegmentSlab)) {
            KERROR(
                "[heap] %s slab/segment kind mismatch pointer=%llx header_kind=%s segment_kind=%s segment=%llx\n",
                reason != NULL ? reason : "lookup",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(pointer)),
                heap_block_kind_name(header->kind),
                heap_segment_kind_name(segment->kind),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)));
            heap_report_corruption_locked(reason);
            return false;
        }
        if (!heap_tail_canary_valid(header)) {
            KERROR(
                "[heap] %s tail canary failed pointer=%llx header=%llx kind=%s size=%llu segment=%llx\n",
                reason != NULL ? reason : "lookup",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(pointer)),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)),
                heap_block_kind_name(header->kind),
                static_cast<unsigned long long>(header->requested_size),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)));
            heap_report_corruption_locked(reason);
            return false;
        }

        if (header_out != NULL) {
            *header_out = header;
        }
        if (segment_out != NULL) {
            *segment_out = segment;
        }
        return true;
    }

    /*
     * Allocate one block from a small fixed-size slab bin.
     *
     * This mirrors the CE fixed-block fast path: once a bin has an available
     * slab at the head, allocation is just a free-list pop plus canary setup.
     *
     * @param bin_index Small-bin index.
     * @param size Requested payload size.
     * @param caller_hint Return address of the requester.
     * @return Allocation pointer on success, or NULL on failure.
     */
    void* heap_alloc_small_locked(U16 bin_index, Size size, Uptr caller_hint) {
        HeapSegment* segment = g_small_bin_available_heads[bin_index];
        HeapBlockHeader* header;

        if (segment == NULL) {
            segment = heap_create_slab_segment(bin_index, caller_hint);
            if (segment == NULL) {
                return NULL;
            }
        }
        if ((segment->block_list == NULL) || (segment->free_count == 0U)) {
            KERROR(
                "[heap] slab head empty caller=%llx bin=%s segment=%llx free=%u block=%llx\n",
                static_cast<unsigned long long>(caller_hint),
                HeapSmallBins[bin_index].name,
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                segment->free_count,
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment->block_list)));
            heap_report_corruption_locked("slab-head-empty");
            return NULL;
        }

        header = segment->block_list;
        segment->block_list = header->next_free;
        header->next_free = NULL;
        header->magic = HeapBlockAllocatedMagic;
        header->requested_size = size;
        segment->free_count -= 1U;
        segment->allocation_count += 1U;
        if (segment->free_count == 0U) {
            heap_small_bin_remove_available(segment);
        }

        fill_bytes(heap_block_payload_pointer(header), HeapAllocatedPoison, segment->user_bytes + HeapTailCanaryBytes);
        heap_write_tail_canary(header);
        HEAP_KDEBUG(
            "[heap] alloc small caller=%llx bin=%s segment=%llx header=%llx payload=%llx size=%llu free=%u alloc=%u\n",
            static_cast<unsigned long long>(caller_hint),
            HeapSmallBins[bin_index].name,
            static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
            static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)),
            static_cast<unsigned long long>(reinterpret_cast<Uptr>(heap_block_payload_pointer(header))),
            static_cast<unsigned long long>(size),
            segment->free_count,
            segment->allocation_count);
        return heap_block_payload_pointer(header);
    }

    /*
     * Allocate one block from the general variable-size free list.
     *
     * This path keeps the linked-list model but removes the old duplicate layout
     * math. Every alignment-sensitive decision goes through the shared layout
     * helper before the segment is split and committed.
     *
     * @param size Requested payload bytes.
     * @param alignment Requested payload alignment.
     * @param caller_hint Return address of the requester.
     * @return Allocation pointer on success, or NULL on failure.
     */
    void* heap_alloc_general_locked(Size size, Size alignment, Uptr caller_hint) {
        HeapSegment* best_segment = NULL;
        HeapAllocationLayout best_layout = {};
        Size best_waste = 0U;

        for (HeapSegment* segment = g_heap_segment_list_head; segment != NULL; segment = segment->next) {
            HeapAllocationLayout layout;

            if (!heap_segment_pointer_valid(segment)) {
                KERROR(
                    "[heap] alloc general corrupt segment caller=%llx head=%llx segment=%llx\n",
                    static_cast<unsigned long long>(caller_hint),
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(g_heap_segment_list_head)),
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)));
                heap_report_corruption_locked("alloc-general-corrupt");
                return NULL;
            }
            if (segment->kind != HeapSegmentFree) {
                continue;
            }

            layout = heap_compute_general_layout(segment, size, alignment);
            if (!layout.valid) {
                continue;
            }

            if ((best_segment == NULL) || ((segment->total_size - layout.prefix_bytes - layout.required_bytes) < best_waste)) {
                best_segment = segment;
                best_layout = layout;
                best_waste = segment->total_size - layout.prefix_bytes - layout.required_bytes;
            }
        }

        if (best_segment == NULL) {
            KERROR(
                "[heap] alloc general failed caller=%llx size=%llu align=%llu\n",
                static_cast<unsigned long long>(caller_hint),
                static_cast<unsigned long long>(size),
                static_cast<unsigned long long>(alignment));
            return NULL;
        }

        if (best_layout.prefix_bytes >= minimum_free_segment_size()) {
            best_segment = heap_split_segment_prefix(best_segment, best_layout.prefix_bytes);
            best_layout = heap_compute_general_layout(best_segment, size, alignment);
        }
        heap_split_segment_tail(best_segment, best_layout.required_bytes);
        heap_mark_segment_allocated(best_segment, HeapSegmentGeneral, HeapInvalidBinIndex);
        best_segment->user_bytes = size;
        best_segment->block_count = 1U;
        best_segment->allocation_count = 1U;

        {
            HeapBlockHeader* header = reinterpret_cast<HeapBlockHeader*>(best_layout.block_header_start);

            memzero(header, sizeof(*header));
            header->magic = HeapBlockAllocatedMagic;
            header->kind = HeapBlockGeneral;
            header->bin_index = HeapInvalidBinIndex;
            header->requested_size = size;
            header->owner_segment = best_segment;
            best_segment->block_list = header;
            heap_write_tail_canary(header);
            HEAP_KDEBUG(
                "[heap] alloc general caller=%llx segment=%llx header=%llx payload=%llx size=%llu align=%llu segbytes=%llu waste=%llu\n",
                static_cast<unsigned long long>(caller_hint),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(best_segment)),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(heap_block_payload_pointer(header))),
                static_cast<unsigned long long>(size),
                static_cast<unsigned long long>(alignment),
                static_cast<unsigned long long>(best_segment->total_size),
                static_cast<unsigned long long>(best_waste));
            return heap_block_payload_pointer(header);
        }
    }

    /*
     * Release one slab allocation back to its owning fixed-size region.
     *
     * Once the slab becomes completely idle, the whole region is returned to the
     * general free list so larger allocations can reclaim that page again.
     *
     * @param segment Owning slab segment.
     * @param header Block header being released.
     * @param caller_hint Return address of the releaser.
     * @return Nothing.
     */
    void heap_free_small_locked(HeapSegment* segment, HeapBlockHeader* header, Uptr caller_hint) {
        const bool was_full = segment->free_count == 0U;

        fill_bytes(heap_block_payload_pointer(header), HeapFreedPoison, segment->user_bytes + HeapTailCanaryBytes);
        header->magic = HeapBlockFreedMagic;
        header->requested_size = 0U;
        header->next_free = segment->block_list;
        segment->block_list = header;
        segment->free_count += 1U;
        segment->allocation_count -= 1U;

        if (was_full) {
            heap_small_bin_insert_available(segment);
        }

        HEAP_KDEBUG(
            "[heap] free small caller=%llx bin=%s segment=%llx header=%llx free=%u alloc=%u\n",
            static_cast<unsigned long long>(caller_hint),
            HeapSmallBins[segment->bin_index].name,
            static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
            static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)),
            segment->free_count,
            segment->allocation_count);

        if (segment->allocation_count == 0U) {
            heap_small_bin_remove_available(segment);
            heap_mark_segment_free(segment, segment->total_size);
            (void)heap_coalesce_free_segment(segment);
            HEAP_KDEBUG(
                "[heap] reclaim empty slab caller=%llx segment=%llx\n",
                static_cast<unsigned long long>(caller_hint),
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)));
        }
    }

    /*
     * Release one general allocation back to the free-segment chain.
     *
     * General frees restore the entire segment to the free state and immediately
     * coalesce adjacent free neighbors to keep the variable-size path healthy.
     *
     * @param segment Owning general segment.
     * @param header Block header being released.
     * @param caller_hint Return address of the releaser.
     * @return Nothing.
     */
    void heap_free_general_locked(HeapSegment* segment, HeapBlockHeader* header, Uptr caller_hint) {
        const Size payload_bytes = heap_general_capacity_bytes(segment, header) + HeapTailCanaryBytes;

        fill_bytes(heap_block_payload_pointer(header), HeapFreedPoison, payload_bytes);
        header->magic = HeapBlockFreedMagic;
        header->requested_size = 0U;
        heap_mark_segment_free(segment, segment->total_size);
        (void)heap_coalesce_free_segment(segment);
        HEAP_KDEBUG(
            "[heap] free general caller=%llx segment=%llx header=%llx\n",
            static_cast<unsigned long long>(caller_hint),
            static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
            static_cast<unsigned long long>(reinterpret_cast<Uptr>(header)));
    }

    /*
     * Try to resize one general allocation in place.
     *
     * The CE heap favors stable pointers when possible. This helper grows into a
     * free right neighbor or shrinks by returning the tail to the free list,
     * avoiding allocate-copy-free when the existing segment can still satisfy
     * the request.
     *
     * @param segment Owning general segment.
     * @param header Live block header.
     * @param pointer Caller-visible payload pointer.
     * @param size New requested size.
     * @return Same pointer on success, or NULL when in-place resize is not possible.
     */
    void* heap_realloc_general_in_place_locked(HeapSegment* segment, HeapBlockHeader* header, void* pointer, Size size) {
        const Uptr segment_start = reinterpret_cast<Uptr>(segment);
        const Uptr payload_start = reinterpret_cast<Uptr>(pointer);
        const Size current_capacity = heap_general_capacity_bytes(segment, header);

        if (size <= current_capacity) {
            const Size required_bytes = align_up(
                static_cast<Size>((payload_start + size + HeapTailCanaryBytes) - segment_start),
                HeapMinimumAlignment);

            if (required_bytes < segment->total_size) {
                heap_split_segment_tail(segment, required_bytes);
                if ((segment->next != NULL) && (segment->next->kind == HeapSegmentFree)) {
                    /*
                     * Shrinking can expose a new free tail directly in front of
                     * an existing free neighbor. Coalesce it immediately so the
                     * segment chain never carries adjacent free extents.
                     */
                    (void)heap_coalesce_free_segment(segment->next);
                }
            }
            segment->user_bytes = size;
            header->requested_size = size;
            heap_write_tail_canary(header);
            HEAP_KDEBUG(
                "[heap] realloc in-place shrink segment=%llx payload=%llx new_size=%llu capacity=%llu\n",
                static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                static_cast<unsigned long long>(payload_start),
                static_cast<unsigned long long>(size),
                static_cast<unsigned long long>(current_capacity));
            return pointer;
        }

        if ((segment->next != NULL) && (segment->next->kind == HeapSegmentFree)) {
            HeapSegment* next = segment->next;
            const Size merged_total = segment->total_size + next->total_size;
            const Size required_bytes = align_up(
                static_cast<Size>((payload_start + size + HeapTailCanaryBytes) - segment_start),
                HeapMinimumAlignment);

            if (required_bytes <= merged_total) {
                segment->total_size = merged_total;
                segment->next = next->next;
                if (next->next != NULL) {
                    next->next->prev = segment;
                }
                heap_split_segment_tail(segment, required_bytes);
                if ((segment->next != NULL) && (segment->next->kind == HeapSegmentFree)) {
                    /*
                     * Growing in place may consume one free neighbor and then
                     * split a smaller free remainder back out. If the segment to
                     * the right was already free, merge the new remainder now to
                     * preserve the no-adjacent-free invariant.
                     */
                    (void)heap_coalesce_free_segment(segment->next);
                }
                segment->user_bytes = size;
                header->requested_size = size;
                heap_write_tail_canary(header);
                HEAP_KDEBUG(
                    "[heap] realloc in-place grow segment=%llx payload=%llx new_size=%llu merged=%llu\n",
                    static_cast<unsigned long long>(reinterpret_cast<Uptr>(segment)),
                    static_cast<unsigned long long>(payload_start),
                    static_cast<unsigned long long>(size),
                    static_cast<unsigned long long>(merged_total));
                return pointer;
            }
        }

        return NULL;
    }

    /*
     * Initialize the allocator from the board-provided bootstrap arena.
     *
     * The heap uses one fully covered segment chain rooted in the reserved early
     * heap span, so initialization is just one free segment covering the arena.
     *
     * @return StatusOK when the heap can serve allocations.
     */
    Status heap_init_impl(void) {
        VirtAddr heap_start;
        const PhysAddr heap_phys = mm::PhysicalMemory::reserved_heap_physical_base();
        const Size heap_bytes = align_up(mm::board_config::EarlyHeapSize, HeapMinimumAlignment);

        if (g_heap_initialized) {
            return StatusOK;
        }
        if ((heap_phys == 0U) || (heap_bytes < minimum_free_segment_size()) || !is_power_of_two(mm::PageSize)) {
            return StatusNotSupported;
        }

        heap_start = mm::MemoryManager::physical_to_kernel(heap_phys);
        g_heap_start_address = static_cast<Uptr>(heap_start);
        g_heap_end_address = g_heap_start_address + heap_bytes;
        memzero(g_small_bin_available_heads, sizeof(g_small_bin_available_heads));

        g_heap_segment_list_head = reinterpret_cast<HeapSegment*>(heap_start);
        heap_mark_segment_free(g_heap_segment_list_head, heap_bytes);
        g_heap_initialized = true;
        KRETAIL(
            "[heap] init base=%llx end=%llx bytes=%llu bins=%llu\n",
            static_cast<unsigned long long>(g_heap_start_address),
            static_cast<unsigned long long>(g_heap_end_address),
            static_cast<unsigned long long>(heap_bytes),
            static_cast<unsigned long long>(COUNT_OF(HeapSmallBins)));
        return StatusOK;
    }

    /*
     * Allocate one heap block using either the slab fast path or the general allocator.
     *
     * Small default-alignment requests first try the fixed-size bins; everything
     * else uses the validated variable-size allocator.
     *
     * @param size Requested payload size.
     * @param alignment Requested payload alignment.
     * @param caller_hint Return address of the caller for diagnostics.
     * @return Allocation pointer on success, or NULL on failure.
     */
    void* heap_alloc_impl(Size size, Size alignment, Uptr caller_hint) {
        void* result = NULL;
        int bin_index;
        const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

        if (!g_heap_initialized || (size == 0U)) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return NULL;
        }
        if (alignment == 0U) {
            alignment = HeapMinimumAlignment;
        }
        if (alignment < HeapMinimumAlignment) {
            alignment = HeapMinimumAlignment;
        }
        if (!is_power_of_two(alignment)) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return NULL;
        }

        bin_index = heap_small_bin_index_for_request(size, alignment);
        if (bin_index >= 0) {
            result = heap_alloc_small_locked(static_cast<U16>(bin_index), size, caller_hint);
        }
        if (result == NULL) {
            result = heap_alloc_general_locked(size, alignment, caller_hint);
        }
        if ((result == NULL) && (heap_grow_locked(size, alignment) == StatusOK)) {
            bin_index = heap_small_bin_index_for_request(size, alignment);
            if (bin_index >= 0) {
                result = heap_alloc_small_locked(static_cast<U16>(bin_index), size, caller_hint);
            }
            if (result == NULL) {
                result = heap_alloc_general_locked(size, alignment, caller_hint);
            }
        }

        arch::Arch::restore_interrupts(interrupts_enabled);
        return result;
    }

    /*
     * Free one previously allocated heap block.
     *
     * The free path validates ownership and canaries before handing the block to
     * either the slab free-list logic or the general coalescing path.
     *
     * @param pointer Allocation to release.
     * @return Nothing.
     */
    void heap_free_impl(void* pointer) {
        HeapBlockHeader* header;
        HeapSegment* segment;
        const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

        if (!g_heap_initialized || (pointer == NULL)) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return;
        }
        if (!heap_lookup_live_allocation_locked(pointer, "free", &header, &segment)) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return;
        }

        if (header->kind == HeapBlockSlab) {
            heap_free_small_locked(segment, header, reinterpret_cast<Uptr>(__builtin_return_address(0)));
        }
        else {
            heap_free_general_locked(segment, header, reinterpret_cast<Uptr>(__builtin_return_address(0)));
        }

        arch::Arch::restore_interrupts(interrupts_enabled);
    }

    /*
     * Reallocate one existing heap block.
     *
     * The allocator keeps the CE behavior of preferring stable pointers but
     * falls back to allocate-copy-free when the current segment cannot satisfy
     * the new size without violating heap invariants.
     *
     * @param pointer Existing allocation, or NULL.
     * @param size Requested new payload size.
     * @return Resized allocation on success, or NULL on failure.
     */
    void* heap_realloc_impl(void* pointer, Size size) {
        HeapBlockHeader* header;
        HeapSegment* segment;
        Size copy_bytes;
        bool interrupts_enabled;
        void* replacement;

        if (pointer == NULL) {
            return heap_alloc_impl(size, 0U, reinterpret_cast<Uptr>(__builtin_return_address(0)));
        }
        if (size == 0U) {
            heap_free_impl(pointer);
            return NULL;
        }

        interrupts_enabled = arch::Arch::save_and_disable_interrupts();
        if (!g_heap_initialized || !heap_lookup_live_allocation_locked(pointer, "realloc", &header, &segment)) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return NULL;
        }

        copy_bytes = header->requested_size;
        if (header->kind == HeapBlockSlab) {
            const int new_bin = heap_small_bin_index_for_request(size, HeapMinimumAlignment);

            if ((new_bin >= 0) && (static_cast<U16>(new_bin) == segment->bin_index)) {
                header->requested_size = size;
                heap_write_tail_canary(header);
                arch::Arch::restore_interrupts(interrupts_enabled);
                return pointer;
            }
        }
        else {
            void* grown = heap_realloc_general_in_place_locked(segment, header, pointer, size);

            if (grown != NULL) {
                arch::Arch::restore_interrupts(interrupts_enabled);
                return grown;
            }
        }

        arch::Arch::restore_interrupts(interrupts_enabled);

        replacement = heap_alloc_impl(size, 0U, reinterpret_cast<Uptr>(__builtin_return_address(0)));
        if (replacement == NULL) {
            return NULL;
        }

        memcopy(replacement, pointer, size < copy_bytes ? size : copy_bytes);
        heap_free_impl(pointer);
        return replacement;
    }

    /*
     * Return one snapshot of heap usage statistics.
     *
     * The statistics are gathered under the allocator lock so callers see a
     * self-consistent view of general segments and slab-region counters.
     *
     * @param stats_out Destination statistics structure.
     * @return Nothing.
     */
    void heap_stats_impl(HeapStats* stats_out) {
        const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

        if (stats_out != NULL) {
            memzero(stats_out, sizeof(*stats_out));
            if (g_heap_initialized) {
                heap_refresh_stats(stats_out);
            }
        }

        arch::Arch::restore_interrupts(interrupts_enabled);
    }

    /*
     * Validate the heap segment chain and report the first broken invariant.
     *
     * Higher-level code uses this helper to bracket risky subsystems and learn
     * who first damaged heap metadata rather than who happened to allocate next.
     *
     * @param reason Short label describing the validation point.
     * @return True when every segment and block remains consistent.
     */
    bool heap_debug_validate_impl(const char* reason) {
        const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();
        const bool valid = heap_validate_locked(reason);

        arch::Arch::restore_interrupts(interrupts_enabled);
        return valid;
    }

} // namespace

/*
 * Initialize the global kernel heap.
 *
 * @return StatusOK when the heap is ready.
 */
Status Heap::init(void) {
    return heap_init_impl();
}

/*
 * Allocate one aligned kernel heap block.
 *
 * @param size Requested payload size.
 * @param alignment Required payload alignment.
 * @return Allocation pointer on success, or NULL on failure.
 */
void* Heap::alloc(Size size, Size alignment) {
    return heap_alloc_impl(size, alignment, reinterpret_cast<Uptr>(__builtin_return_address(0)));
}

/*
 * Resize one existing kernel heap block.
 *
 * @param pointer Existing allocation, or NULL.
 * @param size Requested new payload size.
 * @return Resized allocation on success, or NULL on failure.
 */
void* Heap::realloc(void* pointer, Size size) {
    return heap_realloc_impl(pointer, size);
}

/*
 * Release one prior heap allocation.
 *
 * @param pointer Allocation to release.
 * @return Nothing.
 */
void Heap::free(void* pointer) {
    heap_free_impl(pointer);
}

/*
 * Return one snapshot of current heap usage.
 *
 * @param stats_out Destination statistics structure.
 * @return Nothing.
 */
void Heap::get_stats(HeapStats* stats_out) {
    heap_stats_impl(stats_out);
}

/*
 * Validate allocator metadata without mutating heap state.
 *
 * @param reason Diagnostic label for logs.
 * @return True when the heap remains internally consistent.
 */
bool Heap::debug_validate(const char* reason) {
    return heap_debug_validate_impl(reason);
}