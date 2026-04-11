#include "gui_service.h"

#include "device.h"
#include "framebuffer.h"
#include "heap.h"
#include "kernel_time.h"
#include "mm.h"
#include "mm/physical.h"
#include "scheduler.h"
#include "process.h"
#include "service_call.h"
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

    inline constexpr Size GuiSurfaceRecordCount = ROS_KERNEL_GUI_WINDOW_SURFACE_MAX_SLOTS;
    inline constexpr Size GuiSurfaceMaxBytes = mm::backend::L2BlockSize;
    inline constexpr Size GuiSurfaceSlotUnitBytes = 64U * 1024U;
    inline constexpr Size GuiSurfaceSlotUnitCount = 4096U;
    inline constexpr Size GuiSurfaceViewRegionBytes = GuiSurfaceSlotUnitBytes * GuiSurfaceSlotUnitCount;
    inline constexpr VirtAddr GuiSurfaceViewBase = 64ULL * mm::backend::L2BlockSize;
    inline constexpr VirtAddr GuiSurfaceViewLimit = GuiSurfaceViewBase + GuiSurfaceViewRegionBytes;
    inline constexpr VirtAddr GuiSharedInputViewBase = GuiSurfaceViewLimit;
    inline constexpr Size GuiSharedInputViewBytes = mm::PageSize;
    inline constexpr VirtAddr GuiSharedInputViewLimit = GuiSharedInputViewBase + GuiSharedInputViewBytes;

    static_assert((GuiSurfaceSlotUnitBytes% mm::PageSize) == 0U, "GUI surface slot units must stay page aligned");
    static_assert(GuiSurfaceViewLimit <= (mm::backend::TableEntries * mm::backend::L2BlockSize), "GUI surface region must stay inside the first user GiB");
    static_assert((GuiSharedInputViewBase% mm::PageSize) == 0U, "shared input view must stay page aligned");
    static_assert(GuiSharedInputViewLimit <= (mm::backend::TableEntries * mm::backend::L2BlockSize), "shared input view must stay inside the first user GiB");

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
        bool owner_mapped;
        bool server_mapped;
        void* backing;
        PhysAddr physical_base;
    } SharedWindowSurfaceRecord;

    bool gui_service_initialized;
    SharedWindowSurfaceRecord shared_surfaces[GuiSurfaceRecordCount];
    bool shared_surface_slots[GuiSurfaceSlotUnitCount];

    /* Shared input region (one page) exposed to user-mode GWES and peers. */
    static PhysAddr g_gui_shared_input_phys = 0ULL;
    static RosKernelGuiSharedInputRegion* g_gui_shared_input_region_ptr = nullptr;
    static RosKernelGuiPointerState g_gui_pointer_state = { (uint32_t)ROS_KERNEL_GUI_POINTER_HIDDEN, (uint32_t)ROS_KERNEL_GUI_POINTER_HIDDEN, 0U, 0U };

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
        if (!region) return;
        memzero(region, sizeof(*region));
        region->magic = ROS_KERNEL_GUI_SHARED_INPUT_MAGIC;
        region->version = ROS_KERNEL_GUI_SHARED_INPUT_VERSION;
        region->max_consumers = ROS_KERNEL_GUI_SHARED_INPUT_MAX_CONSUMERS;
        region->capacity = ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY;
        region->record_size = static_cast<uint32_t>(sizeof(RosKernelGuiSharedInputRecord));
        region->tail_sequence = 0ULL;
        region->produced_count = 0ULL;
        region->overflow_count = 0ULL;
        region->pointer_event_count = 0ULL;
        region->key_event_count = 0ULL;
        region->last_pointer_state = g_gui_pointer_state;
        region->reserved0 = 0U;
        region->reserved1 = 0U;
        for (unsigned int i = 0U; i < ROS_KERNEL_GUI_SHARED_INPUT_MAX_CONSUMERS; ++i) {
            gui_shared_input_reset_consumer(&region->consumers[i]);
        }
    }

    static RosKernelGuiSharedInputRegion* gui_shared_input_ensure_region(void) {
        if (g_gui_shared_input_region_ptr) return g_gui_shared_input_region_ptr;
        PhysAddr page = mm::PhysicalMemory::alloc_page();
        if (page == 0ULL) return nullptr;
        g_gui_shared_input_phys = page;
        g_gui_shared_input_region_ptr = reinterpret_cast<RosKernelGuiSharedInputRegion*>(mm::MemoryManager::physical_to_kernel(page));
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

        for (U32 index = 0U; index < ROS_KERNEL_GUI_SHARED_INPUT_MAX_CONSUMERS; ++index) {
            RosKernelGuiSharedInputConsumer* consumer = &region->consumers[index];

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

        for (U32 index = 0U; index < ROS_KERNEL_GUI_SHARED_INPUT_MAX_CONSUMERS; ++index) {
            RosKernelGuiSharedInputConsumer* consumer = &region->consumers[index];
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
            GuiSharedInputViewBase,
            g_gui_shared_input_phys,
            GuiSharedInputViewBytes,
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

        return mm::MemoryManager::unmap(&process->process_address_space, GuiSharedInputViewBase, GuiSharedInputViewBytes);
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

        if ((region == NULL) || (view == NULL) || (consumer_index >= ROS_KERNEL_GUI_SHARED_INPUT_MAX_CONSUMERS)) {
            return StatusInvalidArgument;
        }

        view->version = ROS_KERNEL_GUI_SHARED_INPUT_VERSION;
        view->flags = region->consumers[consumer_index].flags;
        view->view_address = GuiSharedInputViewBase;
        view->view_size = static_cast<U32>(sizeof(RosKernelGuiSharedInputRegion));
        view->consumer_index = consumer_index;
        view->initial_head_sequence = region->consumers[consumer_index].head_sequence;
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
        return GuiSurfaceViewBase + (static_cast<VirtAddr>(slot_index) * GuiSurfaceSlotUnitBytes);
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
        for (Size index = 0U; index < COUNT_OF(shared_surfaces); ++index) {
            SharedWindowSurfaceRecord* record = &shared_surfaces[index];

            if (record->in_use && (record->server_pid == server_pid) && (record->hwnd == hwnd)) {
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

        for (Size index = 0U; index < COUNT_OF(shared_surfaces); ++index) {
            SharedWindowSurfaceRecord* record = &shared_surfaces[index];

            if (!record->in_use || (record->hwnd != hwnd)) {
                continue;
            }
            if ((record->owner_pid == static_cast<I64>(process->id)) || (record->server_pid == static_cast<I64>(process->id))) {
                return record;
            }
        }

        return NULL;
    }

    /*
     * Reserve one empty shared-surface record.
     *
     * Surface metadata and virtual slot units are allocated independently so a
     * large window can span multiple units without forcing the record table to
     * mirror the virtual address allocator.
     *
     * @return Empty record, or NULL when the service is full.
     */
    SharedWindowSurfaceRecord* gui_reserve_surface(void) {
        for (Size index = 0U; index < COUNT_OF(shared_surfaces); ++index) {
            if (!shared_surfaces[index].in_use) {
                return &shared_surfaces[index];
            }
        }

        return NULL;
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
        const U64 slot_count = (static_cast<U64>(allocation_bytes) + (GuiSurfaceSlotUnitBytes - 1ULL)) / GuiSurfaceSlotUnitBytes;

        if ((slot_count_out == NULL) || (allocation_bytes == 0U)) {
            return StatusInvalidArgument;
        }
        if ((slot_count == 0ULL) || (slot_count > GuiSurfaceSlotUnitCount)) {
            return StatusNoSpace;
        }

        *slot_count_out = static_cast<U32>(slot_count);
        return StatusOK;
    }

    /*
     * Reserve one contiguous span of shared-surface slot units.
     *
     * A contiguous first-fit allocator keeps the resulting user mapping stable
     * and lets `map` and `unmap` cover the whole surface with one range even
     * when it crosses several 64 KiB units.
     *
     * @param slot_count Number of contiguous units required.
     * @param slot_index_out Receives the first unit index.
     * @return StatusOK on success, or StatusNoSpace when no contiguous span fits.
     */
    Status gui_reserve_surface_span(U32 slot_count, U32* slot_index_out) {
        U32 run_start = 0U;
        U32 run_length = 0U;

        if ((slot_index_out == NULL) || (slot_count == 0U)) {
            return StatusInvalidArgument;
        }
        if (slot_count > COUNT_OF(shared_surface_slots)) {
            return StatusNoSpace;
        }

        for (U32 index = 0U; index < COUNT_OF(shared_surface_slots); ++index) {
            if (shared_surface_slots[index]) {
                run_length = 0U;
                run_start = index + 1U;
                continue;
            }

            if (run_length == 0U) {
                run_start = index;
            }
            ++run_length;
            if (run_length == slot_count) {
                for (U32 reserve_index = 0U; reserve_index < slot_count; ++reserve_index) {
                    shared_surface_slots[run_start + reserve_index] = true;
                }
                *slot_index_out = run_start;
                return StatusOK;
            }
        }

        return StatusNoSpace;
    }

    /*
     * Release one contiguous span of shared-surface slot units.
     *
     * Surface records own their slot-unit reservation for the entire lifetime
     * of the backing store. Clearing the reservation here makes small-window VA
     * reuse deterministic after destroy or process teardown.
     *
     * @param slot_index First reserved slot-unit index.
     * @param slot_count Number of reserved units.
     * @return Nothing.
     */
    void gui_release_surface_span(U32 slot_index, U32 slot_count) {
        if ((slot_count == 0U) || (slot_index >= COUNT_OF(shared_surface_slots))) {
            return;
        }

        for (U32 index = 0U; (index < slot_count) && ((slot_index + index) < COUNT_OF(shared_surface_slots)); ++index) {
            shared_surface_slots[slot_index + index] = false;
        }
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
        const VmMapping mapping = {
            gui_surface_slot_address(record->slot_index),
            record->physical_base,
            record->allocation_bytes,
            PagePresent | PageWritable | PageUser,
        };

        if ((process == NULL) || (record == NULL)) {
            return StatusInvalidArgument;
        }

        GUI_SURFACE_TRACE(
            "map surface request pid=%ld hwnd=%llu slot=%u vaddr=%llx paddr=%llx len=%llx owner=%ld",
            static_cast<long>(process->id),
            static_cast<unsigned long long>(record->hwnd),
            record->slot_index,
            static_cast<unsigned long long>(mapping.virtual_base),
            static_cast<unsigned long long>(mapping.physical_base),
            static_cast<unsigned long long>(mapping.length),
            static_cast<long>(record->owner_pid));
        return mm::MemoryManager::map(&process->process_address_space, &mapping);
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
    void gui_clear_surface_record(SharedWindowSurfaceRecord* record) {
        if (record == NULL) {
            return;
        }

        if (record->backing != NULL) {
            Heap::free(record->backing);
        }
        gui_release_surface_span(record->slot_index, record->slot_count);
        memzero(record, sizeof(*record));
    }

} // namespace

Status GuiService::init(void) {
    Status status = gui_heap_checkpoint_status("gui:init:entry");

    if (status != StatusOK) {
        return status;
    }

    if (!gui_service_initialized) {
        memzero(shared_surfaces, sizeof(shared_surfaces));
        memzero(shared_surface_slots, sizeof(shared_surface_slots));
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
        for (consumer_index = 0U; consumer_index < ROS_KERNEL_GUI_SHARED_INPUT_MAX_CONSUMERS; ++consumer_index) {
            if (region->consumers[consumer_index].pid == 0U) {
                consumer = &region->consumers[consumer_index];
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

    record = gui_reserve_surface();
    if (record == NULL) {
        return StatusNoSpace;
    }

    status = gui_reserve_surface_span(slot_count, &slot_index);
    if (status != StatusOK) {
        return status;
    }

    memzero(record, sizeof(*record));
    record->backing = Heap::alloc(allocation_bytes, mm::PageSize);
    if (record->backing == NULL) {
        gui_release_surface_span(slot_index, slot_count);
        memzero(record, sizeof(*record));
        return StatusNoMemory;
    }
    status = gui_heap_checkpoint_status("gui:create-surface:backing-ready");
    if (status != StatusOK) {
        gui_clear_surface_record(record);
        return status;
    }

    memzero(record->backing, allocation_bytes);
    record->in_use = true;
    record->hwnd = view->hwnd;
    record->owner_pid = view->owner_pid;
    record->server_pid = static_cast<I64>(process->id);
    record->width = view->width;
    record->height = view->height;
    record->pitch = view->width * sizeof(U32);
    record->pixel_format = view->pixel_format;
    record->allocation_bytes = allocation_bytes;
    record->slot_index = slot_index;
    record->slot_count = slot_count;
    record->physical_base = mm::MemoryManager::kernel_to_physical(reinterpret_cast<VirtAddr>(record->backing));

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
        gui_clear_surface_record(record);
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

    gui_clear_surface_record(record);
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

    for (Size index = 0U; index < COUNT_OF(shared_surfaces); ++index) {
        SharedWindowSurfaceRecord* record = &shared_surfaces[index];

        if (!record->in_use) {
            continue;
        }

        if (record->server_pid == static_cast<I64>(process->id)) {
            Process* owner_process = ProcessManager::find_process(static_cast<Pid>(record->owner_pid));

            if ((owner_process != NULL) && (owner_process != process) && record->owner_mapped) {
                (void)gui_unmap_surface(owner_process, record);
            }
            gui_clear_surface_record(record);
            continue;
        }

        if (record->owner_pid == static_cast<I64>(process->id)) {
            record->owner_mapped = false;
        }
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
    RosKernelGuiSharedInputRecord* record = &region->records[sequence % ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY];

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

    if (sequence >= ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY) {
        region->overflow_count++;
    }

    region->produced_count = sequence + 1ULL;
    asm volatile("dmb ishst" ::: "memory");
    region->tail_sequence = sequence + 1ULL;

    for (U32 index = 0U; index < ROS_KERNEL_GUI_SHARED_INPUT_MAX_CONSUMERS; ++index) {
        RosKernelGuiSharedInputConsumer* consumer = &region->consumers[index];

        if ((consumer->pid == 0U) || (consumer->head_sequence != sequence)) {
            continue;
        }

        gui_notify_shared_input_consumer(consumer, sequence + 1ULL, type);
    }
}