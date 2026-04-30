#include "gui_service.h"

#include "arch.h"
#include "device.h"
#include "framebuffer.h"
#include "heap.h"
#include "kernel_time.h"
#include "mm.h"
#include "mm/physical.h"
#include "process.h"
#include "resource_manager.h"
#include "scheduler.h"
#include "service_call.h"
#include "user_address_space_layout.h"
#include "vfs.h"

#include "debug-message.h"

namespace {

#define GUI_SURFACE_TRACE(fmt, ...) KDEBUG(KZONE_MEMORY, "[gui-surface] " fmt "\n", ##__VA_ARGS__)

    /*
     * Validate heap integrity at one GUI subsystem boundary.
     *
     * The GUI bring-up path is one of the main heap-corruption suspects, so the
     * temporary checkpoints should fail as close as possible to shared-input and
     * surface lifecycle transitions rather than later during unrelated work.
     *
     * @param reason Phase label for the validation log.
     * @return StatusOK when the heap remains internally consistent.
     */
    Status gui_heap_checkpoint_status(const char* reason) {
        if (!Heap::debug_validate(reason)) {
            KERROR("[gui] heap checkpoint failed reason=%s\n", reason != NULL ? reason : "<none>");
            return StatusFault;
        }

        return StatusOK;
    }

    /*
     * Log one concise GUI surface memory snapshot.
     *
     * Surface allocation failures can come from total physical-page pressure,
     * kernel heap exhaustion inside bookkeeping, or leaked live GUI surfaces.
     * Capturing all three counters at the failing branch keeps the next repro
     * actionable instead of forcing another round of guesswork.
     *
     * @param reason Short phase label describing the failure point.
     * @return Nothing.
     */
    void gui_log_surface_memory_snapshot(const char* reason) {
        HeapStats heap_stats = {};
        KernelResourceStats record_stats = {};
        KernelResourceStats backing_stats = {};

        Heap::get_stats(&heap_stats);
        KernelResourceManager::get_stats(KernelResourceKind::GuiSurfaceRecord, &record_stats);
        KernelResourceManager::get_stats(KernelResourceKind::GuiSurfaceBacking, &backing_stats);
        KERROR(
            "[gui-surface] snapshot reason=%s free_pages=%u heap_used=%llu heap_free=%llu records=%llu/%llu backings=%llu/%llu bytes=%llu/%llu\n",
            reason != NULL ? reason : "<none>",
            mm::PhysicalMemory::free_page_count(),
            static_cast<unsigned long long>(heap_stats.used_bytes),
            static_cast<unsigned long long>(heap_stats.free_bytes),
            static_cast<unsigned long long>(record_stats.live_count),
            static_cast<unsigned long long>(record_stats.peak_count),
            static_cast<unsigned long long>(backing_stats.live_count),
            static_cast<unsigned long long>(backing_stats.peak_count),
            static_cast<unsigned long long>(backing_stats.live_bytes),
            static_cast<unsigned long long>(backing_stats.peak_bytes));
    }

    // Explorer now owns the desktop as one real top-level window, so a single
    // client surface can legitimately exceed one 2 MiB L2 block. Allow larger
    // page-backed surfaces while still keeping a finite per-window cap well
    // below the full shared-view arena.
    inline constexpr Size GuiSurfaceMaxBytes = 16U * mm::backend::L2BlockSize;

    /*
     * Round one fixed-record size up to the next aligned slot boundary.
     *
     * The GUI surface metadata pool carves whole physical pages into equal
     * record slots, so each slot stride must preserve the natural alignment of
     * the stored `SharedWindowSurfaceRecord`.
     *
     * @param value Raw record size in bytes.
     * @param alignment Required record alignment.
     * @return Aligned slot size.
     */
    constexpr Size gui_align_up_record(Size value, Size alignment) {
        return (value + (alignment - 1U)) & ~(alignment - 1U);
    }

    static_assert(user_address_space::GuiSurfaceViewLimit <= (mm::backend::TableEntries * mm::backend::L2BlockSize), "GUI surface region must stay inside the first user GiB");
    static_assert(user_address_space::GuiSharedInputViewLimit <= (mm::backend::TableEntries * mm::backend::L2BlockSize), "shared input view must stay inside the first user GiB");
    static_assert(GuiSurfaceMaxBytes <= user_address_space::GuiSurfaceViewRegionBytes, "single GUI surface cap must fit inside the GUI view arena");

    typedef struct SharedWindowSurfaceRecord {
        bool in_use;
        U64 hwnd;
        I64 owner_pid;
        I64 server_pid;
        U32 width;
        U32 height;
        U32 pitch;
        U32 pixel_format;
        U32 allocation_bytes;
        U32 slot_index;
        U32 slot_count;
        U32 backing_page_count;
        bool owner_mapped;
        bool server_mapped;
        void* backing;
        PhysAddr physical_base;
        PhysAddr* backing_pages;
        struct SharedWindowSurfaceRecord* next;
        struct SharedWindowSurfaceRecord* prev;
        struct SharedWindowSurfaceRecord* slot_next;
        struct SharedWindowSurfaceRecord* slot_prev;
    } SharedWindowSurfaceRecord;

    typedef struct GuiSurfaceRecordSlot {
        struct GuiSurfaceRecordSlot* next_free;
    } GuiSurfaceRecordSlot;

    typedef struct GuiSurfaceRecordPageHeader {
        struct GuiSurfaceRecordPageHeader* next_page;
        PhysAddr page_phys;
        U32 live_slots;
        U32 slot_capacity;
    } GuiSurfaceRecordPageHeader;

    typedef struct GuiSurfaceRecordPool {
        Size slot_bytes;
        Size slot_alignment;
        GuiSurfaceRecordSlot* free_list;
        GuiSurfaceRecordPageHeader* pages;
    } GuiSurfaceRecordPool;

    bool gui_service_initialized;
    SharedWindowSurfaceRecord* g_shared_surface_head;
    SharedWindowSurfaceRecord* g_shared_surface_tail;
    SharedWindowSurfaceRecord* g_shared_surface_slot_head;
    SharedWindowSurfaceRecord* g_shared_surface_slot_tail;
    GuiSurfaceRecordPool g_shared_surface_record_pool = {};

    /*
     * Initialize the GUI surface metadata pool lazily.
     *
     * Surface records are only needed after the first shared surface is
     * created, so the pool geometry is derived on demand instead of during
     * broader GUI service initialization.
     *
     * @return Nothing.
     */
    void gui_surface_record_pool_init(void) {
        if (g_shared_surface_record_pool.slot_bytes != 0U) {
            return;
        }

        g_shared_surface_record_pool.slot_alignment = alignof(SharedWindowSurfaceRecord);
        g_shared_surface_record_pool.slot_bytes = gui_align_up_record(sizeof(SharedWindowSurfaceRecord), alignof(SharedWindowSurfaceRecord));
        g_shared_surface_record_pool.free_list = NULL;
        g_shared_surface_record_pool.pages = NULL;
    }

    /*
     * Return the metadata page header that owns one record slot.
     *
     * Each GUI metadata page stores its header at the start of the page and
     * carves `SharedWindowSurfaceRecord` slots from the remaining bytes.
     *
     * @param slot Slot pointer returned by the GUI metadata pool.
     * @return Owning page header, or NULL for invalid input.
     */
    GuiSurfaceRecordPageHeader* gui_surface_record_page_from_slot(const void* slot) {
        if (slot == NULL) {
            return NULL;
        }

        return reinterpret_cast<GuiSurfaceRecordPageHeader*>(
            reinterpret_cast<Uptr>(slot) & ~(static_cast<Uptr>(mm::PageSize) - 1U));
    }

    /*
     * Grow the GUI metadata pool by carving one physical page into record slots.
     *
     * GUI surface records can live for the lifetime of a window, so page-backed
     * slots remove that persistent bookkeeping pressure from the general heap
     * while still allowing empty metadata pages to be reclaimed.
     *
     * @return True when at least one new slot becomes available.
     */
    bool gui_surface_record_pool_grow(void) {
        const PhysAddr page_phys = mm::PhysicalMemory::alloc_page();
        U8* page_base;
        GuiSurfaceRecordPageHeader* page_header;
        Uptr slot_start;
        Size available_bytes;
        Size slot_capacity;

        if ((g_shared_surface_record_pool.slot_bytes == 0U)
            || (g_shared_surface_record_pool.slot_alignment == 0U)
            || (page_phys == 0U)) {
            return false;
        }

        page_base = reinterpret_cast<U8*>(mm::MemoryManager::physical_to_kernel(page_phys));
        page_header = reinterpret_cast<GuiSurfaceRecordPageHeader*>(page_base);
        memzero(page_header, sizeof(*page_header));
        page_header->next_page = g_shared_surface_record_pool.pages;
        page_header->page_phys = page_phys;
        g_shared_surface_record_pool.pages = page_header;

        slot_start = reinterpret_cast<Uptr>(page_base + sizeof(GuiSurfaceRecordPageHeader));
        slot_start = gui_align_up_record(slot_start, g_shared_surface_record_pool.slot_alignment);
        if (slot_start >= (reinterpret_cast<Uptr>(page_base) + mm::PageSize)) {
            g_shared_surface_record_pool.pages = page_header->next_page;
            mm::PhysicalMemory::free_page(page_phys);
            return false;
        }

        available_bytes = (reinterpret_cast<Uptr>(page_base) + mm::PageSize) - slot_start;
        slot_capacity = available_bytes / g_shared_surface_record_pool.slot_bytes;
        if (slot_capacity == 0U) {
            g_shared_surface_record_pool.pages = page_header->next_page;
            mm::PhysicalMemory::free_page(page_phys);
            return false;
        }

        page_header->slot_capacity = static_cast<U32>(slot_capacity);
        for (Size index = 0U; index < slot_capacity; ++index) {
            GuiSurfaceRecordSlot* slot = reinterpret_cast<GuiSurfaceRecordSlot*>(slot_start + (index * g_shared_surface_record_pool.slot_bytes));

            slot->next_free = g_shared_surface_record_pool.free_list;
            g_shared_surface_record_pool.free_list = slot;
        }

        return true;
    }

    /*
     * Allocate one zeroed GUI surface metadata record from the page-backed pool.
     *
     * @return Fresh metadata record, or NULL when no slot can be allocated.
     */
    SharedWindowSurfaceRecord* gui_surface_record_pool_allocate(void) {
        GuiSurfaceRecordSlot* slot;
        GuiSurfaceRecordPageHeader* page_header;

        gui_surface_record_pool_init();
        if ((g_shared_surface_record_pool.free_list == NULL) && !gui_surface_record_pool_grow()) {
            return NULL;
        }

        slot = g_shared_surface_record_pool.free_list;
        g_shared_surface_record_pool.free_list = slot->next_free;
        page_header = gui_surface_record_page_from_slot(slot);
        if (page_header != NULL) {
            page_header->live_slots += 1U;
        }

        memzero(slot, g_shared_surface_record_pool.slot_bytes);
        return reinterpret_cast<SharedWindowSurfaceRecord*>(slot);
    }

    /*
     * Remove every free-list entry that belongs to one reclaimed metadata page.
     *
     * @param page_header Page that is about to be released.
     * @return Nothing.
     */
    void gui_surface_record_pool_remove_page_slots(GuiSurfaceRecordPageHeader* page_header) {
        const Uptr page_start = reinterpret_cast<Uptr>(page_header);
        const Uptr page_limit = page_start + mm::PageSize;
        GuiSurfaceRecordSlot* previous = NULL;
        GuiSurfaceRecordSlot* slot = g_shared_surface_record_pool.free_list;

        if (page_header == NULL) {
            return;
        }

        while (slot != NULL) {
            GuiSurfaceRecordSlot* next_slot = slot->next_free;
            const Uptr slot_address = reinterpret_cast<Uptr>(slot);

            if ((slot_address >= page_start) && (slot_address < page_limit)) {
                if (previous != NULL) {
                    previous->next_free = next_slot;
                }
                else {
                    g_shared_surface_record_pool.free_list = next_slot;
                }
            }
            else {
                previous = slot;
            }

            slot = next_slot;
        }
    }

    /*
     * Unlink one reclaimed metadata page from the pool page list.
     *
     * @param page_header Page header to unlink.
     * @return Nothing.
     */
    void gui_surface_record_pool_unlink_page(GuiSurfaceRecordPageHeader* page_header) {
        GuiSurfaceRecordPageHeader* previous = NULL;
        GuiSurfaceRecordPageHeader* cursor = g_shared_surface_record_pool.pages;

        if (page_header == NULL) {
            return;
        }

        while ((cursor != NULL) && (cursor != page_header)) {
            previous = cursor;
            cursor = cursor->next_page;
        }
        if (cursor == NULL) {
            return;
        }

        if (previous != NULL) {
            previous->next_page = cursor->next_page;
        }
        else {
            g_shared_surface_record_pool.pages = cursor->next_page;
        }
    }

    /*
     * Return one surface metadata record to the page-backed pool.
     *
     * @param record Record previously allocated from the metadata pool.
     * @return Nothing.
     */
    void gui_surface_record_pool_free(SharedWindowSurfaceRecord* record) {
        GuiSurfaceRecordSlot* slot;
        GuiSurfaceRecordPageHeader* page_header;

        if (record == NULL) {
            return;
        }

        slot = reinterpret_cast<GuiSurfaceRecordSlot*>(record);
        page_header = gui_surface_record_page_from_slot(record);
        slot->next_free = g_shared_surface_record_pool.free_list;
        g_shared_surface_record_pool.free_list = slot;

        if ((page_header == NULL) || (page_header->live_slots == 0U)) {
            return;
        }

        page_header->live_slots -= 1U;
        if (page_header->live_slots != 0U) {
            return;
        }

        gui_surface_record_pool_remove_page_slots(page_header);
        gui_surface_record_pool_unlink_page(page_header);
        mm::PhysicalMemory::free_page(page_header->page_phys);
    }

    /* Shared input region (one page) exposed to user-mode GWES and peers. */
    static PhysAddr g_gui_shared_input_phys = 0ULL;
    static RosKernelGuiSharedInputRegion* g_gui_shared_input_region_ptr = nullptr;
    static RosKernelGuiPointerState g_gui_pointer_state = { (uint32_t)ROS_KERNEL_GUI_POINTER_HIDDEN, (uint32_t)ROS_KERNEL_GUI_POINTER_HIDDEN, 0U, 0U };

    /*
     * Compute how many shared-input consumers fit inside the reserved view.
     *
     * The shared ring keeps a fixed record capacity, so the remaining reserved
     * bytes can now be devoted entirely to consumer descriptors instead of a
     * compile-time fixed array embedded in the ABI.
     *
     * @return Runtime consumer capacity derived from the reserved view size.
     */
    U32 gui_shared_input_max_consumers(void) {
        const Size minimum_bytes = sizeof(RosKernelGuiSharedInputRegion)
            + (sizeof(RosKernelGuiSharedInputRecord) * ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY);

        if (user_address_space::GuiSharedInputViewBytes <= minimum_bytes) {
            return 0U;
        }

        return static_cast<U32>(
            (user_address_space::GuiSharedInputViewBytes - minimum_bytes)
            / sizeof(RosKernelGuiSharedInputConsumer));
    }

    /*
     * Post one coalesced wake notification to a shared-input consumer.
     *
     * The consumer only needs a nudge when the shared-input ring transitions
     * from empty to non-empty for that PID. Subsequent events remain visible in
     * the mapped ring without sending redundant broker traffic.
     *
     * @param consumer Shared-input consumer record to wake.
     * @param next_sequence Tail sequence after the just-published event.
     * @param type Event type that triggered the wake.
     * @return Nothing.
     */
    static void gui_notify_shared_input_consumer(const RosKernelGuiSharedInputConsumer* consumer, U64 next_sequence, uint32_t type) {
        if ((consumer == NULL) || (consumer->pid == 0U)) {
            return;
        }

        (void)service_kernel_ipc_notify(
            consumer->pid,
            ROS_KERNEL_GUI_PROTOCOL,
            ROS_KERNEL_GUI_KIND_SHARED_INPUT_READY,
            static_cast<unsigned long>(next_sequence),
            static_cast<unsigned long>(type),
            0UL,
            0UL);
    }

    static void gui_shared_input_reset_consumer(RosKernelGuiSharedInputConsumer* consumer) {
        if (!consumer) return;
        consumer->pid = 0U;
        consumer->flags = 0U;
        consumer->head_sequence = 0ULL;
        consumer->drop_count = 0ULL;
        consumer->last_seen_msec = 0U;
        consumer->reserved = 0U;
    }

    static void gui_shared_input_initialize_region(RosKernelGuiSharedInputRegion* region) {
        const U32 max_consumers = gui_shared_input_max_consumers();

        if (!region) return;
        memzero(region, user_address_space::GuiSharedInputViewBytes);
        region->magic = ROS_KERNEL_GUI_SHARED_INPUT_MAGIC;
        region->version = ROS_KERNEL_GUI_SHARED_INPUT_VERSION;
        region->max_consumers = static_cast<uint16_t>(max_consumers);
        region->capacity = ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY;
        region->record_size = static_cast<uint32_t>(sizeof(RosKernelGuiSharedInputRecord));
        region->consumer_size = static_cast<uint32_t>(sizeof(RosKernelGuiSharedInputConsumer));
        region->records_offset = static_cast<uint32_t>(sizeof(RosKernelGuiSharedInputRegion));
        region->consumers_offset = region->records_offset + (region->capacity * region->record_size);
        region->tail_sequence = 0ULL;
        region->produced_count = 0ULL;
        region->overflow_count = 0ULL;
        region->pointer_event_count = 0ULL;
        region->key_event_count = 0ULL;
        region->last_pointer_state = g_gui_pointer_state;
        region->reserved0 = 0U;
        region->reserved1 = 0U;
        for (U32 index = 0U; index < max_consumers; ++index) {
            gui_shared_input_reset_consumer(ros_kernel_gui_shared_input_consumer_at(region, index));
        }
    }

    static RosKernelGuiSharedInputRegion* gui_shared_input_ensure_region(void) {
        if (g_gui_shared_input_region_ptr) return g_gui_shared_input_region_ptr;
        const Size rounded_bytes = user_address_space::GuiSharedInputViewBytes;
        const unsigned int page_count = static_cast<unsigned int>(rounded_bytes / mm::PageSize);
        const PhysAddr base_phys = mm::PhysicalMemory::reserve_contiguous_pages(page_count);

        if ((gui_shared_input_max_consumers() == 0U) || (base_phys == 0ULL)) return nullptr;
        g_gui_shared_input_phys = base_phys;
        g_gui_shared_input_region_ptr = reinterpret_cast<RosKernelGuiSharedInputRegion*>(mm::MemoryManager::physical_to_kernel(base_phys));
        gui_shared_input_initialize_region(g_gui_shared_input_region_ptr);
        return g_gui_shared_input_region_ptr;
    }

    /*
     * Report whether one process object still represents a live consumer slot.
     *
     * @param process Candidate process pointer.
     * @return True when the process exists and has not reached final teardown.
     */
    bool gui_process_is_live(const Process* process) {
        return (process != NULL)
            && (process->current_state != ProcessState::Exiting)
            && (process->current_state != ProcessState::Terminated);
    }

    /*
     * Find one shared-input consumer record by PID.
     *
     * @param pid Consumer PID to search.
     * @param consumer_index_out Optional slot index output.
     * @return Matching consumer slot, or NULL when the PID is not attached.
     */
    RosKernelGuiSharedInputConsumer* gui_find_shared_input_consumer(U64 pid, U32* consumer_index_out) {
        RosKernelGuiSharedInputRegion* region = gui_shared_input_ensure_region();

        if ((region == NULL) || (pid == 0ULL)) {
            return NULL;
        }

        for (U32 index = 0U; index < region->max_consumers; ++index) {
            RosKernelGuiSharedInputConsumer* consumer = ros_kernel_gui_shared_input_consumer_at(region, index);

            if (consumer->pid == static_cast<U32>(pid)) {
                if (consumer_index_out != NULL) {
                    *consumer_index_out = index;
                }
                return consumer;
            }
        }

        return NULL;
    }

    /*
     * Forget any shared-input consumers whose owning process already exited.
     *
     * @return Nothing.
     */
    void gui_cleanup_stale_shared_input_consumers(void) {
        RosKernelGuiSharedInputRegion* region = gui_shared_input_ensure_region();

        if (region == NULL) {
            return;
        }

        for (U32 index = 0U; index < region->max_consumers; ++index) {
            RosKernelGuiSharedInputConsumer* consumer = ros_kernel_gui_shared_input_consumer_at(region, index);
            Process* process;

            if (consumer->pid == 0U) {
                continue;
            }

            process = ProcessManager::find_process(static_cast<Pid>(consumer->pid));
            if (!gui_process_is_live(process)) {
                gui_shared_input_reset_consumer(consumer);
            }
        }
    }

    /*
     * Map the shared-input page into one user process at the fixed GUI view.
     *
     * @param process Destination process.
     * @return StatusOK on success, or the propagated MMU failure code.
     */
    Status gui_map_shared_input(Process* process) {
        const VmMapping mapping = {
            user_address_space::GuiSharedInputViewBase,
            g_gui_shared_input_phys,
            user_address_space::GuiSharedInputViewBytes,
            PagePresent | PageWritable | PageUser,
        };

        if ((process == NULL) || (g_gui_shared_input_phys == 0ULL)) {
            return StatusInvalidArgument;
        }

        return mm::MemoryManager::map(&process->process_address_space, &mapping);
    }

    /*
     * Remove the fixed shared-input mapping from one user process.
     *
     * @param process Process currently holding the shared-input view.
     * @return StatusOK on success, or the propagated MMU failure code.
     */
    Status gui_unmap_shared_input(Process* process) {
        if (process == NULL) {
            return StatusInvalidArgument;
        }

        return mm::MemoryManager::unmap(&process->process_address_space, user_address_space::GuiSharedInputViewBase, user_address_space::GuiSharedInputViewBytes);
    }

    /*
     * Populate one public shared-input view for an attached consumer.
     *
     * @param consumer_index Slot index that belongs to the requesting process.
     * @param view Receives the fixed mapping description.
     * @return StatusOK on success, or StatusInvalidArgument when the slot is invalid.
     */
    Status gui_fill_shared_input_view(U32 consumer_index, RosKernelGuiSharedInputView* view) {
        RosKernelGuiSharedInputRegion* region = gui_shared_input_ensure_region();

        RosKernelGuiSharedInputConsumer* consumer;

        if ((region == NULL) || (view == NULL) || (consumer_index >= region->max_consumers)) {
            return StatusInvalidArgument;
        }

        consumer = ros_kernel_gui_shared_input_consumer_at(region, consumer_index);

        view->version = ROS_KERNEL_GUI_SHARED_INPUT_VERSION;
        view->flags = consumer->flags;
        view->view_address = user_address_space::GuiSharedInputViewBase;
        view->view_size = static_cast<U32>(user_address_space::GuiSharedInputViewBytes);
        view->consumer_index = consumer_index;
        view->initial_head_sequence = consumer->head_sequence;
        return StatusOK;
    }

    /*
    * Return the user virtual base used for one shared surface slot unit.
     *
    * Small windows should not consume a full 2 MiB hole in every client and
    * GWES address space. The kernel now reserves view space in 64 KiB units
    * while still mapping the exact page-rounded surface size, which preserves
    * stable user pointers without forcing one L2-sized gap per window.
     *
     * @param slot_index Shared surface slot-unit number.
     * @return User virtual base for that slot.
     */
    VirtAddr gui_surface_slot_address(U32 slot_index) {
        return user_address_space::GuiSurfaceViewBase + (static_cast<VirtAddr>(slot_index) * user_address_space::GuiSurfaceSlotUnitBytes);
    }

    /*
     * Resolve the framebuffer device exposed through the VFS alias.
     *
     * @param device_out Receives the device pointer on success.
     * @return StatusOK on success, or the propagated VFS/device failure.
     */
    Status gui_resolve_framebuffer(Device** device_out) {
        VfsNode node;
        Status status;

        if (device_out == NULL) {
            return StatusInvalidArgument;
        }

        memzero(&node, sizeof(node));
        status = VirtualFileSystem::resolve("FRAMEBUFFER:", &node);
        if (status != StatusOK) {
            return status;
        }
        if ((node.backend_kind != VfsBackendKindDevice) || (node.device == NULL)) {
            return StatusNotFound;
        }

        *device_out = node.device;
        return StatusOK;
    }

    /*
     * Translate a framebuffer backend format into the public GUI ABI format.
     *
     * @param framebuffer_format Native framebuffer format identifier.
     * @return ABI pixel-format constant consumed by user mode.
     */
    U32 gui_translate_pixel_format(U32 framebuffer_format) {
        if (framebuffer_format == FramebufferPixelFormatXrgb8888) {
            return ROS_KERNEL_GUI_PIXEL_FORMAT_XRGB8888;
        }

        return ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888;
    }

    /*
     * Find one existing shared surface by server PID and window handle.
     *
     * @param server_pid GWES PID that created the surface.
     * @param hwnd Stable GWES window handle.
     * @return Matching surface record, or NULL when none exists.
     */
    SharedWindowSurfaceRecord* gui_find_surface_by_server(I64 server_pid, U64 hwnd) {
        for (SharedWindowSurfaceRecord* record = g_shared_surface_head; record != NULL; record = record->next) {
            if ((record != NULL) && record->in_use && (record->server_pid == server_pid) && (record->hwnd == hwnd)) {
                return record;
            }
        }

        return NULL;
    }

    /*
     * Find one surface that the current process is allowed to map.
     *
     * @param process Requesting process.
     * @param hwnd Stable window handle.
     * @return Matching surface record, or NULL when access is not allowed.
     */
    SharedWindowSurfaceRecord* gui_find_surface_for_process(Process* process, U64 hwnd) {
        if ((process == NULL) || (hwnd == 0U)) {
            return NULL;
        }

        for (SharedWindowSurfaceRecord* record = g_shared_surface_head; record != NULL; record = record->next) {
            if ((record == NULL) || !record->in_use || (record->hwnd != hwnd)) {
                continue;
            }
            if ((record->owner_pid == static_cast<I64>(process->id)) || (record->server_pid == static_cast<I64>(process->id))) {
                return record;
            }
        }

        return NULL;
    }

    /*
     * Link one live surface record into the global intrusive registry.
     *
     * Surface metadata records are already page-backed objects, so the registry
     * now publishes them directly instead of storing extra heap-backed pointer
     * slots in a separate dynamic array.
     *
     * @param record Live surface record that is ready for lookup.
     * @return Nothing.
     */
    void gui_link_surface_record(SharedWindowSurfaceRecord* record) {
        if (record == NULL) {
            return;
        }

        record->prev = g_shared_surface_tail;
        record->next = NULL;
        if (g_shared_surface_tail != NULL) {
            g_shared_surface_tail->next = record;
        }
        else {
            g_shared_surface_head = record;
        }

        g_shared_surface_tail = record;
    }

    /*
     * Unlink one surface record from the global intrusive registry.
     *
     * @param record Published surface record to remove.
     * @return Nothing.
     */
    void gui_unlink_surface_record(SharedWindowSurfaceRecord* record) {
        if (record == NULL) {
            return;
        }

        if (record->prev != NULL) {
            record->prev->next = record->next;
        }
        else if (g_shared_surface_head == record) {
            g_shared_surface_head = record->next;
        }

        if (record->next != NULL) {
            record->next->prev = record->prev;
        }
        else if (g_shared_surface_tail == record) {
            g_shared_surface_tail = record->prev;
        }

        record->next = NULL;
        record->prev = NULL;
    }

    /*
     * Allocate one page-backed surface metadata record.
     *
     * Surface view space is already capped by the reserved EL0 arena, so the
     * metadata registry should grow with demand without consuming additional
     * long-lived allocations from the general heap.
     *
     * @param owner_id Diagnostic owner identifier for resource tracking.
     * @return Fresh metadata record, or NULL when allocation or tracking fails.
     */
    SharedWindowSurfaceRecord* gui_allocate_surface_record(U64 owner_id) {
        SharedWindowSurfaceRecord* record;
        Status status;
        const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

        record = gui_surface_record_pool_allocate();
        arch::Arch::restore_interrupts(interrupts_enabled);
        if (record == NULL) {
            return NULL;
        }

        status = KernelResourceManager::track_external(
            KernelResourceKind::GuiSurfaceRecord,
            record,
            g_shared_surface_record_pool.slot_bytes,
            owner_id,
            "gui-surface-record");
        if (status != StatusOK) {
            const bool rollback_interrupts = arch::Arch::save_and_disable_interrupts();

            gui_surface_record_pool_free(record);
            arch::Arch::restore_interrupts(rollback_interrupts);
            return NULL;
        }

        return record;
    }

    /*
     * Return one surface metadata record to the page-backed pool.
     *
     * @param record Surface metadata record to release.
     * @return Nothing.
     */
    void gui_free_surface_record(SharedWindowSurfaceRecord* record) {
        if (record == NULL) {
            return;
        }

        const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

        (void)KernelResourceManager::untrack(KernelResourceKind::GuiSurfaceRecord, record);
        gui_surface_record_pool_free(record);
        arch::Arch::restore_interrupts(interrupts_enabled);
    }

    /*
     * Allocate one page list for a GUI surface backing.
     *
     * Requiring one physically contiguous run for every long-lived window
     * surface makes GWES fragile once one large surface is already alive.
     * Using one page list instead lets later windows and popup menus consume
     * any free pages that remain, which is the behavior the shared surface
     * arena needs.
     *
     * @param backing_bytes Page-rounded surface byte count.
     * @param owner_id Diagnostic owner identifier for resource tracking.
     * @param page_count_out Receives the number of pages in the returned list.
     * @return Heap-owned physical page list, or NULL on failure.
     */
    PhysAddr* gui_allocate_surface_backing_pages(U32 backing_bytes, U64 owner_id, U32* page_count_out) {
        const Size rounded_bytes = (static_cast<Size>(backing_bytes) + (mm::PageSize - 1U)) & ~(mm::PageSize - 1U);
        const U32 page_count = static_cast<U32>(rounded_bytes / mm::PageSize);
        PhysAddr* backing_pages;

        if ((backing_bytes == 0U) || (page_count_out == NULL)) {
            return NULL;
        }
        if ((rounded_bytes == 0U) || (page_count == 0U)) {
            return NULL;
        }

        backing_pages = static_cast<PhysAddr*>(Heap::alloc(static_cast<Size>(page_count) * sizeof(PhysAddr), alignof(PhysAddr)));
        if (backing_pages == NULL) {
            KERROR("[gui-surface] backing-page-list alloc failed bytes=%u pages=%u owner=%llu\n",
                backing_bytes,
                page_count,
                static_cast<unsigned long long>(owner_id));
            gui_log_surface_memory_snapshot("backing-page-list-alloc-failed");
            return NULL;
        }

        memzero(backing_pages, static_cast<Size>(page_count) * sizeof(PhysAddr));
        for (U32 page_index = 0U; page_index < page_count; ++page_index) {
            void* page_alias;
            Status status;

            backing_pages[page_index] = mm::PhysicalMemory::alloc_page();
            if (backing_pages[page_index] == 0U) {
                KERROR("[gui-surface] backing-page alloc failed bytes=%u pages=%u owner=%llu page_index=%u\n",
                    backing_bytes,
                    page_count,
                    static_cast<unsigned long long>(owner_id),
                    page_index);
                gui_log_surface_memory_snapshot("backing-page-alloc-failed");
                for (U32 rollback_index = 0U; rollback_index < page_index; ++rollback_index) {
                    if (backing_pages[rollback_index] != 0U) {
                        page_alias = reinterpret_cast<void*>(mm::MemoryManager::physical_to_kernel(backing_pages[rollback_index]));
                        (void)KernelResourceManager::untrack(KernelResourceKind::GuiSurfaceBacking, page_alias);
                        mm::PhysicalMemory::free_page(backing_pages[rollback_index]);
                    }
                }
                Heap::free(backing_pages);
                return NULL;
            }

            page_alias = reinterpret_cast<void*>(mm::MemoryManager::physical_to_kernel(backing_pages[page_index]));
            status = KernelResourceManager::track_external(
                KernelResourceKind::GuiSurfaceBacking,
                page_alias,
                mm::PageSize,
                owner_id,
                "gui-surface-backing");
            if (status != StatusOK) {
                KERROR("[gui-surface] backing-page track failed bytes=%u pages=%u owner=%llu page_index=%u status=%d\n",
                    backing_bytes,
                    page_count,
                    static_cast<unsigned long long>(owner_id),
                    page_index,
                    static_cast<int>(status));
                gui_log_surface_memory_snapshot("backing-page-track-failed");
                (void)KernelResourceManager::untrack(KernelResourceKind::GuiSurfaceBacking, page_alias);
                mm::PhysicalMemory::free_page(backing_pages[page_index]);
                for (U32 rollback_index = 0U; rollback_index < page_index; ++rollback_index) {
                    if (backing_pages[rollback_index] != 0U) {
                        page_alias = reinterpret_cast<void*>(mm::MemoryManager::physical_to_kernel(backing_pages[rollback_index]));
                        (void)KernelResourceManager::untrack(KernelResourceKind::GuiSurfaceBacking, page_alias);
                        mm::PhysicalMemory::free_page(backing_pages[rollback_index]);
                    }
                }
                Heap::free(backing_pages);
                return NULL;
            }
        }

        *page_count_out = page_count;
        return backing_pages;
    }

    /*
     * Return one GUI surface backing page list.
     *
     * @param backing_pages Physical pages that back the surface.
     * @param page_count Number of pages stored in `backing_pages`.
     * @return Nothing.
     */
    void gui_free_surface_backing_pages(PhysAddr* backing_pages, U32 page_count) {
        if (backing_pages == NULL) {
            return;
        }

        for (U32 page_index = 0U; page_index < page_count; ++page_index) {
            if (backing_pages[page_index] != 0U) {
                void* page_alias = reinterpret_cast<void*>(mm::MemoryManager::physical_to_kernel(backing_pages[page_index]));

                (void)KernelResourceManager::untrack(KernelResourceKind::GuiSurfaceBacking, page_alias);
                mm::PhysicalMemory::free_page(backing_pages[page_index]);
            }
        }

        Heap::free(backing_pages);
    }

    /*
     * Zero one page-backed GUI surface backing before it is published.
     *
     * The surface is shared with both GWES and the client process, so each
     * page must start from known pixels even though the backing is no longer a
     * single contiguous direct-map span.
     *
     * @param backing_pages Physical page list that backs the surface.
     * @param page_count Number of pages stored in `backing_pages`.
     * @return Nothing.
     */
    void gui_zero_surface_backing_pages(const PhysAddr* backing_pages, U32 page_count) {
        if (backing_pages == NULL) {
            return;
        }

        for (U32 page_index = 0U; page_index < page_count; ++page_index) {
            if (backing_pages[page_index] != 0U) {
                void* page_alias = reinterpret_cast<void*>(mm::MemoryManager::physical_to_kernel(backing_pages[page_index]));

                memzero(page_alias, mm::PageSize);
            }
        }
    }

    /*
     * Publish one live surface record in the intrusive registry.
     *
     * @param record Live surface record that is ready for lookup.
     * @return StatusOK on success.
     */
    Status gui_publish_surface_record(SharedWindowSurfaceRecord* record) {
        if (record == NULL) {
            return StatusInvalidArgument;
        }

        gui_link_surface_record(record);
        return StatusOK;
    }

    /*
     * Link one reserved surface span into the slot-ordered registry.
     *
     * The surface slot registry is ordered by `slot_index` so the allocator can
     * find the first fitting gap without relying on a fixed bitmap sized to the
     * full virtual address range.
     *
     * @param record Surface record that already owns a valid slot span.
     * @return Nothing.
     */
    void gui_link_surface_slot_record(SharedWindowSurfaceRecord* record) {
        SharedWindowSurfaceRecord* cursor;

        if ((record == NULL) || (record->slot_count == 0U)) {
            return;
        }

        cursor = g_shared_surface_slot_head;
        while ((cursor != NULL) && (cursor->slot_index <= record->slot_index)) {
            cursor = cursor->slot_next;
        }

        if (cursor == NULL) {
            record->slot_prev = g_shared_surface_slot_tail;
            record->slot_next = NULL;
            if (g_shared_surface_slot_tail != NULL) {
                g_shared_surface_slot_tail->slot_next = record;
            }
            else {
                g_shared_surface_slot_head = record;
            }

            g_shared_surface_slot_tail = record;
            return;
        }

        record->slot_next = cursor;
        record->slot_prev = cursor->slot_prev;
        if (cursor->slot_prev != NULL) {
            cursor->slot_prev->slot_next = record;
        }
        else {
            g_shared_surface_slot_head = record;
        }

        cursor->slot_prev = record;
    }

    /*
     * Unlink one reserved surface span from the slot-ordered registry.
     *
     * @param record Surface record whose reservation should be removed.
     * @return Nothing.
     */
    void gui_unlink_surface_slot_record(SharedWindowSurfaceRecord* record) {
        if (record == NULL) {
            return;
        }

        if (record->slot_prev != NULL) {
            record->slot_prev->slot_next = record->slot_next;
        }
        else if (g_shared_surface_slot_head == record) {
            g_shared_surface_slot_head = record->slot_next;
        }

        if (record->slot_next != NULL) {
            record->slot_next->slot_prev = record->slot_prev;
        }
        else if (g_shared_surface_slot_tail == record) {
            g_shared_surface_slot_tail = record->slot_prev;
        }

        record->slot_next = NULL;
        record->slot_prev = NULL;
    }

    /*
     * Convert one page-rounded surface allocation into slot units.
     *
     * The kernel still caps any single surface at 2 MiB, but dividing the
     * virtual view region into 64 KiB units lets small windows consume only the
     * portion they need instead of a dedicated 2 MiB slot.
     *
     * @param allocation_bytes Page-rounded surface size.
     * @param slot_count_out Receives the required contiguous unit count.
     * @return StatusOK on success, or an error when the request is invalid.
     */
    Status gui_surface_slot_units(U32 allocation_bytes, U32* slot_count_out) {
        const U64 slot_count = (static_cast<U64>(allocation_bytes) + (user_address_space::GuiSurfaceSlotUnitBytes - 1ULL)) / user_address_space::GuiSurfaceSlotUnitBytes;

        if ((slot_count_out == NULL) || (allocation_bytes == 0U)) {
            return StatusInvalidArgument;
        }
        if ((slot_count == 0ULL) || (slot_count > user_address_space::GuiSurfaceSlotUnitCount)) {
            return StatusNoSpace;
        }

        *slot_count_out = static_cast<U32>(slot_count);
        return StatusOK;
    }

    /*
     * Reserve one contiguous span of shared-surface slot units.
     *
     * A first-fit scan over the slot-ordered registry keeps the resulting user
     * mapping stable and lets `map` and `unmap` cover the whole surface with
     * one range even when it crosses several 64 KiB units.
     *
     * @param slot_count Number of contiguous units required.
     * @param slot_index_out Receives the first unit index.
     * @return StatusOK on success, or StatusNoSpace when no contiguous span fits.
     */
    Status gui_reserve_surface_span(U32 slot_count, U32* slot_index_out) {
        U32 candidate = 0U;

        if ((slot_index_out == NULL) || (slot_count == 0U)) {
            return StatusInvalidArgument;
        }
        if (slot_count > user_address_space::GuiSurfaceSlotUnitCount) {
            return StatusNoSpace;
        }

        for (SharedWindowSurfaceRecord* record = g_shared_surface_slot_head; record != NULL; record = record->slot_next) {
            if ((static_cast<U64>(candidate) + static_cast<U64>(slot_count)) <= record->slot_index) {
                *slot_index_out = candidate;
                return StatusOK;
            }

            candidate = record->slot_index + record->slot_count;
        }

        if ((static_cast<U64>(candidate) + static_cast<U64>(slot_count)) <= user_address_space::GuiSurfaceSlotUnitCount) {
            *slot_index_out = candidate;
            return StatusOK;
        }

        return StatusNoSpace;
    }

    /*
     * Release one reserved shared-surface span from the slot registry.
     *
     * Surface records own their slot-unit reservation for the entire lifetime
     * of the backing store. Unlinking the record here makes small-window VA
     * reuse deterministic after destroy or process teardown.
     *
     * @param record Surface record whose reserved span should be released.
     * @return Nothing.
     */
    void gui_release_surface_span(SharedWindowSurfaceRecord* record) {
        if ((record == NULL) || (record->slot_count == 0U)) {
            return;
        }

        gui_unlink_surface_slot_record(record);
    }

    /*
     * Compute the byte size used by one window surface payload.
     *
     * @param width Surface width in pixels.
     * @param height Surface height in pixels.
     * @param bytes_out Receives the computed byte count.
     * @return StatusOK on success, or an error when the surface is too large.
     */
    Status gui_surface_payload_bytes(U32 width, U32 height, U32* bytes_out) {
        const U64 pitch = static_cast<U64>(width) * sizeof(U32);
        const U64 bytes = pitch * static_cast<U64>(height);

        if ((bytes_out == NULL) || (width == 0U) || (height == 0U)) {
            return StatusInvalidArgument;
        }
        if ((pitch > 0xFFFFFFFFULL) || (bytes == 0ULL) || (bytes > GuiSurfaceMaxBytes)) {
            return StatusNoSpace;
        }

        *bytes_out = static_cast<U32>(bytes);
        return StatusOK;
    }

    /*
     * Round one payload size up to the page-aligned allocation used by the MMU.
     *
     * The public ABI still reports the exact drawable byte count, but the
     * kernel only backs the surface with the minimum whole-page span required
     * for mapping into GWES and the owning client.
     *
     * @param payload_bytes Exact drawable byte count.
     * @param allocation_bytes_out Receives the rounded allocation size.
     * @return StatusOK on success, or StatusNoSpace when the slot would overflow.
     */
    Status gui_surface_allocation_bytes(U32 payload_bytes, U32* allocation_bytes_out) {
        const U64 rounded_bytes = (static_cast<U64>(payload_bytes) + (mm::PageSize - 1ULL)) & ~(mm::PageSize - 1ULL);

        if ((allocation_bytes_out == NULL) || (payload_bytes == 0U)) {
            return StatusInvalidArgument;
        }
        if ((rounded_bytes == 0ULL) || (rounded_bytes > GuiSurfaceMaxBytes)) {
            return StatusNoSpace;
        }

        *allocation_bytes_out = static_cast<U32>(rounded_bytes);
        return StatusOK;
    }

    /*
     * Map one surface backing block into the target process.
     *
     * @param process Destination process.
     * @param record Surface record being mapped.
     * @return StatusOK on success, or the MMU failure code.
     */
    Status gui_map_surface(Process* process, const SharedWindowSurfaceRecord* record) {
        VmMapping mapping = {};
        const VirtAddr view_base = gui_surface_slot_address(record->slot_index);

        if ((process == NULL) || (record == NULL) || (record->backing_pages == NULL) || (record->backing_page_count == 0U)) {
            return StatusInvalidArgument;
        }

        GUI_SURFACE_TRACE(
            "map surface request pid=%ld hwnd=%llu slot=%u vaddr=%llx paddr=%llx len=%llx owner=%ld",
            static_cast<long>(process->id),
            static_cast<unsigned long long>(record->hwnd),
            record->slot_index,
            static_cast<unsigned long long>(view_base),
            static_cast<unsigned long long>(record->physical_base),
            static_cast<unsigned long long>(record->allocation_bytes),
            static_cast<long>(record->owner_pid));

        for (U32 page_index = 0U; page_index < record->backing_page_count; ++page_index) {
            mapping.virtual_base = view_base + (static_cast<VirtAddr>(page_index) * mm::PageSize);
            mapping.physical_base = record->backing_pages[page_index];
            mapping.length = mm::PageSize;
            mapping.flags = PagePresent | PageWritable | PageUser;

            Status status = mm::MemoryManager::map(&process->process_address_space, &mapping);
            if (status != StatusOK) {
                if (page_index != 0U) {
                    (void)mm::MemoryManager::unmap(&process->process_address_space, view_base, static_cast<Size>(page_index) * mm::PageSize);
                }
                return status;
            }
        }

        return StatusOK;
    }

    /*
     * Remove one mapped surface view from the target process.
     *
     * @param process Process currently holding the view.
     * @param record Surface record being unmapped.
     * @return StatusOK on success, or the MMU failure code.
     */
    Status gui_unmap_surface(Process* process, const SharedWindowSurfaceRecord* record) {
        if ((process == NULL) || (record == NULL)) {
            return StatusInvalidArgument;
        }

        GUI_SURFACE_TRACE(
            "unmap surface request pid=%ld hwnd=%llu slot=%u vaddr=%llx paddr=%llx len=%llx owner=%ld",
            static_cast<long>(process->id),
            static_cast<unsigned long long>(record->hwnd),
            record->slot_index,
            static_cast<unsigned long long>(gui_surface_slot_address(record->slot_index)),
            static_cast<unsigned long long>(record->physical_base),
            static_cast<unsigned long long>(record->allocation_bytes),
            static_cast<long>(record->owner_pid));

        return mm::MemoryManager::unmap(&process->process_address_space, gui_surface_slot_address(record->slot_index), record->allocation_bytes);
    }

    /*
     * Populate one public view structure from an internal surface record.
     *
     * @param record Source surface record.
     * @param view_address User virtual address mapped into the caller.
     * @param view_out Receives the public view.
     * @return Nothing.
     */
    void gui_fill_surface_view(const SharedWindowSurfaceRecord* record, VirtAddr view_address, GuiWindowSurfaceView* view_out) {
        if ((record == NULL) || (view_out == NULL)) {
            return;
        }

        view_out->version = ROS_KERNEL_GUI_WINDOW_SURFACE_VIEW_VERSION;
        view_out->flags = ROS_KERNEL_GUI_WINDOW_SURFACE_FLAG_MAPPED;
        view_out->hwnd = record->hwnd;
        view_out->owner_pid = record->owner_pid;
        view_out->view_address = view_address;
        view_out->view_size = record->pitch * record->height;
        view_out->slot_index = record->slot_index;
        view_out->width = record->width;
        view_out->height = record->height;
        view_out->pitch = record->pitch;
        view_out->pixel_format = record->pixel_format;
    }

    /*
    * Release one surface record, its slot span, and its backing block.
     *
     * @param record Surface slot to clear.
     * @return Nothing.
     */
    void gui_release_surface_record_resources(SharedWindowSurfaceRecord* record) {
        if (record == NULL) {
            return;
        }

        if (record->backing != NULL) {
            gui_free_surface_backing_pages(record->backing_pages, record->backing_page_count);
            record->backing = NULL;
        }
        record->backing_pages = NULL;
        record->backing_page_count = 0U;
        gui_release_surface_span(record);
        record->slot_index = 0U;
        record->slot_count = 0U;
        record->allocation_bytes = 0U;
        record->physical_base = 0ULL;
        record->owner_mapped = false;
        record->server_mapped = false;
        record->in_use = false;
    }

    /*
     * Remove one surface record from the registry and free its backing.
     *
     * @param record Surface metadata to destroy.
     * @return Nothing.
     */
    void gui_destroy_surface_record(SharedWindowSurfaceRecord* record) {
        if (record == NULL) {
            return;
        }

        gui_unlink_surface_record(record);
        gui_release_surface_record_resources(record);
        gui_free_surface_record(record);
    }

} // namespace

Status GuiService::init(void) {
    Status status = gui_heap_checkpoint_status("gui:init:entry");

    if (status != StatusOK) {
        return status;
    }

    if (!gui_service_initialized) {
        (void)KernelResourceManager::init();
        g_shared_surface_head = NULL;
        g_shared_surface_tail = NULL;
        g_shared_surface_slot_head = NULL;
        g_shared_surface_slot_tail = NULL;
        gui_service_initialized = true;
    }

    return gui_heap_checkpoint_status("gui:init:success");
}

Status GuiService::query_display_info(RosKernelGuiDisplayInfo* info) {
    Device* device = NULL;
    FramebufferGeometry geometry = {};
    Status status;

    if (info == NULL) {
        return StatusInvalidArgument;
    }

    status = init();
    if (status != StatusOK) {
        return status;
    }

    status = gui_resolve_framebuffer(&device);
    if (status != StatusOK) {
        return status;
    }

    status = device->ioctl(FramebufferIoctlGetGeometry, &geometry);
    if (status != StatusOK) {
        return status;
    }

    info->version = ROS_KERNEL_GUI_DISPLAY_INFO_VERSION;
    info->width = geometry.width;
    info->height = geometry.height;
    info->pitch = geometry.pitch;
    info->pixel_format = gui_translate_pixel_format(geometry.pixel_format);
    return StatusOK;
}

Status GuiService::present_buffer(const RosKernelGuiPresentBuffer* buffer) {
    Device* device = NULL;
    FramebufferGeometry geometry = {};
    const U8* source;
    Status status;

    if ((buffer == NULL) || (buffer->version != ROS_KERNEL_GUI_PRESENT_BUFFER_VERSION)) {
        return StatusInvalidArgument;
    }
    if ((buffer->width == 0U) || (buffer->height == 0U) || (buffer->pixels == 0ULL)) {
        return StatusInvalidArgument;
    }

    status = init();
    if (status != StatusOK) {
        return status;
    }

    status = gui_resolve_framebuffer(&device);
    if (status != StatusOK) {
        return status;
    }

    status = device->ioctl(FramebufferIoctlGetGeometry, &geometry);
    if (status != StatusOK) {
        return status;
    }
    if ((buffer->pitch < (buffer->width * sizeof(U32)))
        || (buffer->x >= geometry.width)
        || (buffer->y >= geometry.height)
        || (buffer->width > (geometry.width - buffer->x))
        || (buffer->height > (geometry.height - buffer->y))) {
        return StatusInvalidArgument;
    }

    source = reinterpret_cast<const U8*>(static_cast<Uptr>(buffer->pixels));
    for (U32 row = 0U; row < buffer->height; ++row) {
        const U64 framebuffer_offset = (static_cast<U64>(buffer->y + row) * geometry.pitch) + (static_cast<U64>(buffer->x) * sizeof(U32));
        const SSize write_result = device->write(framebuffer_offset, source + (static_cast<Size>(row) * buffer->pitch), static_cast<Size>(buffer->width) * sizeof(U32));

        if (write_result < 0) {
            return static_cast<Status>(write_result);
        }
        if (static_cast<Size>(write_result) != static_cast<Size>(buffer->width) * sizeof(U32)) {
            return StatusIoError;
        }
    }

    return StatusOK;
}

Status GuiService::acquire_shared_input(Process* process, RosKernelGuiSharedInputView* view) {
    RosKernelGuiSharedInputRegion* region;
    RosKernelGuiSharedInputConsumer* consumer;
    U32 consumer_index = 0U;
    Status status;

    if ((process == NULL) || (view == NULL)) {
        return StatusInvalidArgument;
    }

    status = gui_heap_checkpoint_status("gui:acquire-shared-input:entry");
    if (status != StatusOK) {
        return status;
    }

    status = init();
    if (status != StatusOK) {
        return status;
    }

    region = gui_shared_input_ensure_region();
    if (region == NULL) {
        return StatusNoMemory;
    }

    gui_cleanup_stale_shared_input_consumers();
    consumer = gui_find_shared_input_consumer(process->id, &consumer_index);
    if (consumer == NULL) {
        for (consumer_index = 0U; consumer_index < region->max_consumers; ++consumer_index) {
            RosKernelGuiSharedInputConsumer* candidate = ros_kernel_gui_shared_input_consumer_at(region, consumer_index);

            if (candidate->pid == 0U) {
                consumer = candidate;
                gui_shared_input_reset_consumer(consumer);
                consumer->pid = static_cast<U32>(process->id);
                consumer->head_sequence = region->tail_sequence;
                break;
            }
        }
    }

    if (consumer == NULL) {
        return StatusNoSpace;
    }

    consumer->last_seen_msec = static_cast<U32>(KernelTime::ticks_to_milliseconds(Scheduler::tick_count()));
    status = gui_map_shared_input(process);
    if ((status != StatusOK) && (status != StatusAlreadyExists)) {
        if (consumer->head_sequence == region->tail_sequence) {
            gui_shared_input_reset_consumer(consumer);
        }
        return status;
    }

    status = gui_fill_shared_input_view(consumer_index, view);
    if (status != StatusOK) {
        return status;
    }

    return gui_heap_checkpoint_status("gui:acquire-shared-input:success");
}

Status GuiService::release_shared_input(Process* process) {
    RosKernelGuiSharedInputConsumer* consumer;
    Status status;

    if (process == NULL) {
        return StatusInvalidArgument;
    }

    status = gui_heap_checkpoint_status("gui:release-shared-input:entry");
    if (status != StatusOK) {
        return status;
    }

    consumer = gui_find_shared_input_consumer(process->id, NULL);
    if (consumer != NULL) {
        gui_shared_input_reset_consumer(consumer);
    }

    status = gui_unmap_shared_input(process);
    if ((status != StatusOK) && (status != StatusNotSupported)) {
        return status;
    }

    if (consumer == NULL) {
        return StatusNotFound;
    }

    return gui_heap_checkpoint_status("gui:release-shared-input:success");
}

Status GuiService::query_shared_input(Process* process, RosKernelGuiSharedInputView* view) {
    RosKernelGuiSharedInputConsumer* consumer;
    U32 consumer_index = 0U;
    Status status;

    if ((process == NULL) || (view == NULL)) {
        return StatusInvalidArgument;
    }

    status = gui_heap_checkpoint_status("gui:query-shared-input:entry");
    if (status != StatusOK) {
        return status;
    }

    status = init();
    if (status != StatusOK) {
        return status;
    }

    gui_cleanup_stale_shared_input_consumers();
    consumer = gui_find_shared_input_consumer(process->id, &consumer_index);
    if (consumer == NULL) {
        return StatusNotFound;
    }

    consumer->last_seen_msec = static_cast<U32>(KernelTime::ticks_to_milliseconds(Scheduler::tick_count()));
    status = gui_fill_shared_input_view(consumer_index, view);
    if (status != StatusOK) {
        return status;
    }

    return gui_heap_checkpoint_status("gui:query-shared-input:success");
}

Status GuiService::create_window_surface(Process* process, GuiWindowSurfaceView* view) {
    SharedWindowSurfaceRecord* record;
    U32 payload_bytes = 0U;
    U32 allocation_bytes = 0U;
    U32 slot_index;
    U32 slot_count;
    Status status;

    if ((process == NULL) || (view == NULL) || (view->version != ROS_KERNEL_GUI_WINDOW_SURFACE_VIEW_VERSION)) {
        return StatusInvalidArgument;
    }
    if ((view->hwnd == 0ULL) || (view->owner_pid <= 0) || (view->width == 0U) || (view->height == 0U)) {
        GUI_SURFACE_TRACE(
            "create invalid hwnd=%llu owner=%ld size=%ux%u version=%u",
            static_cast<unsigned long long>(view != NULL ? view->hwnd : 0ULL),
            static_cast<long>(view != NULL ? view->owner_pid : 0L),
            view != NULL ? view->width : 0U,
            view != NULL ? view->height : 0U,
            view != NULL ? view->version : 0U);
        return StatusInvalidArgument;
    }

    GUI_SURFACE_TRACE(
        "create request server_pid=%ld hwnd=%llu owner=%ld size=%ux%u fmt=%u",
        static_cast<long>(process->id),
        static_cast<unsigned long long>(view->hwnd),
        static_cast<long>(view->owner_pid),
        view->width,
        view->height,
        view->pixel_format);

    status = init();
    if (status != StatusOK) {
        return status;
    }
    status = gui_heap_checkpoint_status("gui:create-surface:entry");
    if (status != StatusOK) {
        return status;
    }

    status = gui_surface_payload_bytes(view->width, view->height, &payload_bytes);
    if (status != StatusOK) {
        return status;
    }
    status = gui_surface_allocation_bytes(payload_bytes, &allocation_bytes);
    if (status != StatusOK) {
        return status;
    }
    status = gui_surface_slot_units(allocation_bytes, &slot_count);
    if (status != StatusOK) {
        return status;
    }
    if ((view->pixel_format != ROS_KERNEL_GUI_PIXEL_FORMAT_XRGB8888)
        && (view->pixel_format != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888)) {
        return StatusInvalidArgument;
    }
    if (gui_find_surface_by_server(static_cast<I64>(process->id), view->hwnd) != NULL) {
        return StatusAlreadyExists;
    }

    record = gui_allocate_surface_record(static_cast<U64>(process->id));
    if (record == NULL) {
        KERROR("[gui-surface] surface-record alloc failed server_pid=%ld hwnd=%llu owner=%ld size=%ux%u\n",
            static_cast<long>(process->id),
            static_cast<unsigned long long>(view->hwnd),
            static_cast<long>(view->owner_pid),
            view->width,
            view->height);
        gui_log_surface_memory_snapshot("surface-record-alloc-failed");
        return StatusNoMemory;
    }

    status = gui_reserve_surface_span(slot_count, &slot_index);
    if (status != StatusOK) {
        gui_free_surface_record(record);
        return status;
    }

    record->slot_index = slot_index;
    record->slot_count = slot_count;
    gui_link_surface_slot_record(record);

    record->backing_pages = gui_allocate_surface_backing_pages(allocation_bytes, static_cast<U64>(process->id), &record->backing_page_count);
    if (record->backing_pages == NULL) {
        KERROR("[gui-surface] surface-backing alloc failed server_pid=%ld hwnd=%llu owner=%ld size=%ux%u bytes=%u pages=%u\n",
            static_cast<long>(process->id),
            static_cast<unsigned long long>(view->hwnd),
            static_cast<long>(view->owner_pid),
            view->width,
            view->height,
            allocation_bytes,
            static_cast<unsigned int>(allocation_bytes / mm::PageSize));
        gui_log_surface_memory_snapshot("surface-backing-alloc-failed");
        gui_release_surface_span(record);
        record->slot_index = 0U;
        record->slot_count = 0U;
        gui_free_surface_record(record);
        return StatusNoMemory;
    }
    record->backing = reinterpret_cast<void*>(mm::MemoryManager::physical_to_kernel(record->backing_pages[0]));
    status = gui_heap_checkpoint_status("gui:create-surface:backing-ready");
    if (status != StatusOK) {
        gui_destroy_surface_record(record);
        return status;
    }

    gui_zero_surface_backing_pages(record->backing_pages, record->backing_page_count);
    record->in_use = true;
    record->hwnd = view->hwnd;
    record->owner_pid = view->owner_pid;
    record->server_pid = static_cast<I64>(process->id);
    record->width = view->width;
    record->height = view->height;
    record->pitch = view->width * sizeof(U32);
    record->pixel_format = view->pixel_format;
    record->allocation_bytes = allocation_bytes;
    record->physical_base = record->backing_pages[0];

    GUI_SURFACE_TRACE(
        "allocated slot=%u span=%u backing=%p paddr=%llx paddr_align=%llx payload=%u bytes=%u pitch=%u",
        record->slot_index,
        record->slot_count,
        record->backing,
        static_cast<unsigned long long>(record->physical_base),
        static_cast<unsigned long long>(record->physical_base & (mm::PageSize - 1U)),
        payload_bytes,
        record->allocation_bytes,
        record->pitch);

    status = gui_map_surface(process, record);
    if (status != StatusOK) {
        GUI_SURFACE_TRACE(
            "map failed pid=%ld hwnd=%llu slot=%u status=%d",
            static_cast<long>(process->id),
            static_cast<unsigned long long>(record->hwnd),
            record->slot_index,
            static_cast<int>(status));
        gui_destroy_surface_record(record);
        return status;
    }

    status = gui_publish_surface_record(record);
    if (status != StatusOK) {
        (void)gui_unmap_surface(process, record);
        gui_destroy_surface_record(record);
        return status;
    }

    record->server_mapped = true;
    gui_fill_surface_view(record, gui_surface_slot_address(record->slot_index), view);
    return gui_heap_checkpoint_status("gui:create-surface:success");
}

Status GuiService::destroy_window_surface(Process* process, const GuiWindowSurfaceView* view) {
    SharedWindowSurfaceRecord* record;
    Process* owner_process;
    Status status;

    if ((process == NULL) || (view == NULL) || (view->hwnd == 0ULL)) {
        return StatusInvalidArgument;
    }

    status = gui_heap_checkpoint_status("gui:destroy-surface:entry");
    if (status != StatusOK) {
        return status;
    }

    record = gui_find_surface_by_server(static_cast<I64>(process->id), view->hwnd);
    if (record == NULL) {
        return StatusNotFound;
    }

    if (record->server_mapped) {
        (void)gui_unmap_surface(process, record);
    }
    owner_process = ProcessManager::find_process(static_cast<Pid>(record->owner_pid));
    if ((owner_process != NULL) && record->owner_mapped && (owner_process != process)) {
        (void)gui_unmap_surface(owner_process, record);
    }

    gui_destroy_surface_record(record);
    return gui_heap_checkpoint_status("gui:destroy-surface:success");
}

Status GuiService::acquire_window_surface(Process* process, GuiWindowSurfaceView* view) {
    SharedWindowSurfaceRecord* record;
    Status status;

    if ((process == NULL) || (view == NULL) || (view->version != ROS_KERNEL_GUI_WINDOW_SURFACE_VIEW_VERSION) || (view->hwnd == 0ULL)) {
        return StatusInvalidArgument;
    }

    status = gui_heap_checkpoint_status("gui:acquire-surface:entry");
    if (status != StatusOK) {
        return status;
    }

    record = gui_find_surface_for_process(process, view->hwnd);
    if (record == NULL) {
        return StatusNotFound;
    }

    status = gui_map_surface(process, record);
    if (status != StatusOK) {
        return status;
    }

    if (record->owner_pid == static_cast<I64>(process->id)) {
        record->owner_mapped = true;
    }
    if (record->server_pid == static_cast<I64>(process->id)) {
        record->server_mapped = true;
    }

    gui_fill_surface_view(record, gui_surface_slot_address(record->slot_index), view);
    return gui_heap_checkpoint_status("gui:acquire-surface:success");
}

Status GuiService::release_window_surface(Process* process, const GuiWindowSurfaceView* view) {
    SharedWindowSurfaceRecord* record;
    Status status;

    if ((process == NULL) || (view == NULL) || (view->hwnd == 0ULL)) {
        return StatusInvalidArgument;
    }

    status = gui_heap_checkpoint_status("gui:release-surface:entry");
    if (status != StatusOK) {
        return status;
    }

    record = gui_find_surface_for_process(process, view->hwnd);
    if (record == NULL) {
        return StatusNotFound;
    }

    status = gui_unmap_surface(process, record);
    if ((status != StatusOK) && (status != StatusNotSupported)) {
        return status;
    }

    if (record->owner_pid == static_cast<I64>(process->id)) {
        record->owner_mapped = false;
    }
    if (record->server_pid == static_cast<I64>(process->id)) {
        record->server_mapped = false;
    }

    return gui_heap_checkpoint_status("gui:release-surface:success");
}

Status GuiService::release_process_surfaces(Process* process) {
    Status status;

    (void)release_shared_input(process);

    if (process == NULL) {
        return StatusInvalidArgument;
    }

    status = gui_heap_checkpoint_status("gui:release-process-surfaces:entry");
    if (status != StatusOK) {
        return status;
    }

    for (SharedWindowSurfaceRecord* record = g_shared_surface_head; record != NULL;) {
        SharedWindowSurfaceRecord* next_record = record->next;

        if (!record->in_use) {
            record = next_record;
            continue;
        }

        if (record->server_pid == static_cast<I64>(process->id)) {
            Process* owner_process = ProcessManager::find_process(static_cast<Pid>(record->owner_pid));

            if ((owner_process != NULL) && (owner_process != process) && record->owner_mapped) {
                (void)gui_unmap_surface(owner_process, record);
            }
            gui_destroy_surface_record(record);
            record = next_record;
            continue;
        }

        if (record->owner_pid == static_cast<I64>(process->id)) {
            record->owner_mapped = false;
        }

        record = next_record;
    }

    return gui_heap_checkpoint_status("gui:release-process-surfaces:success");
}

/*
 * Publish one input event into the shared input region exposed to GWES and
 * other consumers. Kernel producers (keyboard / pointer drivers) should call
 * this to record events; the ring buffer keeps separate counters for pointer
 * and key events so consumers can reason about event types efficiently.
 */
void GuiService::publish_input_event(uint32_t type, uint32_t x, uint32_t y, uint32_t key, uint32_t buttons) {
    RosKernelGuiSharedInputRegion* region = gui_shared_input_ensure_region();
    if (!region) {
        return;
    }

    U64 sequence = region->tail_sequence;
    RosKernelGuiSharedInputRecord* record = ros_kernel_gui_shared_input_record_at(
        region,
        static_cast<uint32_t>(sequence % region->capacity));

    record->sequence = sequence;
    record->uptime_msec = static_cast<uint32_t>(KernelTime::ticks_to_milliseconds(Scheduler::tick_count()));
    record->reserved0 = 0U;

    record->event.version = ROS_KERNEL_GUI_INPUT_EVENT_VERSION;
    record->event.type = type;
    record->event.x = x;
    record->event.y = y;
    record->event.key = key;
    record->event.buttons = buttons;
    record->event.reserved = 0U;
    record->reserved1 = 0U;

    if (type == ROS_KERNEL_GUI_INPUT_EVENT_KEY_DOWN || type == ROS_KERNEL_GUI_INPUT_EVENT_KEY_UP) {
        region->key_event_count++;
    }
    else {
        region->pointer_event_count++;
        region->last_pointer_state.x = x;
        region->last_pointer_state.y = y;
        region->last_pointer_state.pressed = (buttons != 0U) ? 1U : 0U;
        region->last_pointer_state.visible = 1U;
    }

    if (sequence >= region->capacity) {
        region->overflow_count++;
    }

    region->produced_count = sequence + 1ULL;
    asm volatile("dmb ishst" ::: "memory");
    region->tail_sequence = sequence + 1ULL;

    for (U32 index = 0U; index < region->max_consumers; ++index) {
        RosKernelGuiSharedInputConsumer* consumer = ros_kernel_gui_shared_input_consumer_at(region, index);

        if ((consumer->pid == 0U) || (consumer->head_sequence != sequence)) {
            continue;
        }

        gui_notify_shared_input_consumer(consumer, sequence + 1ULL, type);
    }
}