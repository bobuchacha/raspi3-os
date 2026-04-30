/**
 * file_mapping.cpp
 *
 * This code provides File Mapping functionality for the kernel to manage memory-mapped files.
 * It includes functions to create, map, and unmap file mappings, as well as to handle shared
 * memory regions for inter-process communication.
 */

#include "file_mapping.h"

#include "arch.h"
#include "heap.h"
#include "mm.h"
#include "mm/physical.h"
#include "process.h"
#include "user_address_space_layout.h"

namespace FileMapping {
    namespace {
        inline constexpr Size FileMappingPathCapacity = 260U;

        typedef struct FileMappingObject {
            bool in_use;
            bool dirty;
            U16 reserved0;
            U32 reserved1;
            U64 handle_value;
            U32 handle_count;
            U32 attachment_count;
            Size requested_size;
            Size backing_bytes;
            Size backing_page_count;
            PhysAddr* backing_pages;
            char file_path[FileMappingPathCapacity];
            FileMappingObject* next;
            FileMappingObject* prev;
        } FileMappingObject;

        typedef struct FileMappingAttachment {
            bool in_use;
            U8 reserved0;
            U16 reserved1;
            U32 slot_index;
            Process* process;
            FileMappingObject* object;
            VirtAddr view_address;
            Size view_bytes;
            FileMappingAttachment* next;
            FileMappingAttachment* prev;
        } FileMappingAttachment;

        typedef struct FileMappingRecordSlot {
            struct FileMappingRecordSlot* next_free;
        } FileMappingRecordSlot;

        typedef struct FileMappingRecordPageHeader {
            PhysAddr page_phys;
            struct FileMappingRecordPageHeader* next_page;
            U32 live_slots;
            U32 slot_capacity;
        } FileMappingRecordPageHeader;

        typedef struct FileMappingRecordPool {
            Size slot_bytes;
            Size slot_alignment;
            FileMappingRecordSlot* free_list;
            FileMappingRecordPageHeader* pages;
        } FileMappingRecordPool;

        /**
         * Round one fixed-record size up to the next aligned slot boundary.
         *
         * The file-mapping metadata pools carve whole physical pages into equal
         * slots, so every slot stride must preserve the natural alignment of
         * the record type stored in the pool.
         *
         * @param value Raw record size in bytes.
         * @param alignment Required slot alignment.
         * @return Aligned slot size.
         */
        constexpr Size align_up_record(Size value, Size alignment) {
            return (value + (alignment - 1U)) & ~(alignment - 1U);
        }

        bool g_file_mapping_initialized = false;
        U64 g_file_mapping_next_handle = 1ULL;
        FileMappingObject* g_file_mapping_head = NULL;
        FileMappingObject* g_file_mapping_tail = NULL;
        FileMappingAttachment* g_file_mapping_attachment_head = NULL;
        FileMappingAttachment* g_file_mapping_attachment_tail = NULL;
        FileMappingRecordPool g_file_mapping_object_pool = {};
        FileMappingRecordPool g_file_mapping_attachment_pool = {};

        /**
         * Copy one path into fixed storage.
         *
         * @param destination Target buffer.
         * @param capacity Destination capacity.
         * @param source Source text.
         * @return Nothing.
         */
        void copy_path(char* destination, Size capacity, const char* source) {
            Size index = 0U;

            if ((destination == NULL) || (capacity == 0U)) {
                return;
            }

            if (source == NULL) {
                source = "";
            }

            while ((source[index] != '\0') && ((index + 1U) < capacity)) {
                destination[index] = source[index];
                ++index;
            }

            destination[index] = '\0';
        }

        /**
         * Compare two ASCII strings case-insensitively.
         *
         * @param lhs Left-hand string.
         * @param rhs Right-hand string.
         * @return True when the strings match.
         */
        bool same_text_case_insensitive(const char* lhs, const char* rhs) {
            Size index = 0U;

            if (lhs == rhs) {
                return true;
            }
            if ((lhs == NULL) || (rhs == NULL)) {
                return false;
            }

            while ((lhs[index] != '\0') && (rhs[index] != '\0')) {
                const char left = ((lhs[index] >= 'A') && (lhs[index] <= 'Z')) ? static_cast<char>(lhs[index] - 'A' + 'a') : lhs[index];
                const char right = ((rhs[index] >= 'A') && (rhs[index] <= 'Z')) ? static_cast<char>(rhs[index] - 'A' + 'a') : rhs[index];

                if (left != right) {
                    return false;
                }
                ++index;
            }

            return lhs[index] == rhs[index];
        }

        /**
         * Round one byte count up to the next page boundary.
         *
         * The runtime variant keeps the call sites readable when mapping views
         * or reserving payload backing.
         *
         * @param value Requested size.
         * @return Page-aligned size.
         */
        Size align_up_to_page(Size value) {
            const Size mask = mm::PageSize - 1U;

            if (value == 0U) {
                return 0U;
            }

            return (value + mask) & ~mask;
        }

        /**
         * Initialize one file-mapping metadata pool lazily.
         *
         * File-mapping objects and attachments are fixed-size kernel records, so
         * a page-backed pool keeps their storage off the general heap while still
         * allowing empty metadata pages to be reclaimed.
         *
         * @param pool Pool state to initialize.
         * @param record_bytes Raw record size in bytes.
         * @param record_alignment Natural record alignment.
         * @return Nothing.
         */
        void file_mapping_record_pool_init(FileMappingRecordPool* pool, Size record_bytes, Size record_alignment) {
            if ((pool == NULL) || (record_bytes == 0U) || (record_alignment == 0U)) {
                return;
            }
            if (pool->slot_bytes != 0U) {
                return;
            }

            pool->slot_alignment = record_alignment;
            pool->slot_bytes = align_up_record(record_bytes, record_alignment);
            if (pool->slot_bytes < sizeof(FileMappingRecordSlot)) {
                pool->slot_bytes = align_up_record(sizeof(FileMappingRecordSlot), record_alignment);
            }
            pool->free_list = NULL;
            pool->pages = NULL;
        }

        /**
         * Return the pool page header that owns one metadata slot.
         *
         * @param slot Slot pointer previously returned by a file-mapping pool.
         * @return Owning page header, or NULL for invalid input.
         */
        FileMappingRecordPageHeader* file_mapping_record_page_from_slot(const void* slot) {
            if (slot == NULL) {
                return NULL;
            }

            return reinterpret_cast<FileMappingRecordPageHeader*>(
                reinterpret_cast<Uptr>(slot) & ~(static_cast<Uptr>(mm::PageSize) - 1U));
        }

        /**
         * Grow one file-mapping metadata pool by carving one page into slots.
         *
         * The metadata is long-lived shared kernel state, so the pool grows one
         * page at a time and publishes each slot through a free list instead of
         * imposing one compile-time object ceiling.
         *
         * @param pool Pool to extend.
         * @return True when at least one new slot was added.
         */
        bool file_mapping_record_pool_grow(FileMappingRecordPool* pool) {
            const PhysAddr page_phys = mm::PhysicalMemory::alloc_page();
            U8* page_base;
            FileMappingRecordPageHeader* page_header;
            Uptr slot_start;
            Size available_bytes;
            Size slot_capacity;

            if ((pool == NULL) || (pool->slot_bytes == 0U) || (pool->slot_alignment == 0U) || (page_phys == 0U)) {
                return false;
            }

            page_base = reinterpret_cast<U8*>(mm::MemoryManager::physical_to_kernel(page_phys));
            page_header = reinterpret_cast<FileMappingRecordPageHeader*>(page_base);
            page_header->page_phys = page_phys;
            page_header->next_page = pool->pages;
            page_header->live_slots = 0U;
            page_header->slot_capacity = 0U;
            pool->pages = page_header;

            slot_start = reinterpret_cast<Uptr>(page_base + sizeof(FileMappingRecordPageHeader));
            slot_start = align_up_record(slot_start, pool->slot_alignment);
            if (slot_start >= (reinterpret_cast<Uptr>(page_base) + mm::PageSize)) {
                pool->pages = page_header->next_page;
                mm::PhysicalMemory::free_page(page_phys);
                return false;
            }

            available_bytes = (reinterpret_cast<Uptr>(page_base) + mm::PageSize) - slot_start;
            slot_capacity = available_bytes / pool->slot_bytes;
            if (slot_capacity == 0U) {
                pool->pages = page_header->next_page;
                mm::PhysicalMemory::free_page(page_phys);
                return false;
            }

            page_header->slot_capacity = static_cast<U32>(slot_capacity);
            for (Size slot_index = 0U; slot_index < slot_capacity; ++slot_index) {
                FileMappingRecordSlot* slot = reinterpret_cast<FileMappingRecordSlot*>(slot_start + (slot_index * pool->slot_bytes));

                slot->next_free = pool->free_list;
                pool->free_list = slot;
            }

            return true;
        }

        /**
         * Allocate one zeroed metadata record from a page-backed pool.
         *
         * @param pool Pool supplying the record.
         * @return Zeroed record slot, or NULL when the pool cannot grow.
         */
        void* file_mapping_record_pool_allocate(FileMappingRecordPool* pool) {
            FileMappingRecordSlot* slot;
            FileMappingRecordPageHeader* page_header;

            if (pool == NULL) {
                return NULL;
            }
            if ((pool->free_list == NULL) && !file_mapping_record_pool_grow(pool)) {
                return NULL;
            }

            slot = pool->free_list;
            pool->free_list = slot->next_free;
            page_header = file_mapping_record_page_from_slot(slot);
            if (page_header != NULL) {
                ++page_header->live_slots;
            }
            memzero(slot, pool->slot_bytes);
            return slot;
        }

        /**
         * Remove every free-list slot that belongs to one retiring page.
         *
         * A page can only be released once all of its slots are free, so the
         * global free list must drop every slot that points into that page
         * before the physical page is returned to the allocator.
         *
         * @param pool Pool whose free list is being filtered.
         * @param retired_page Page about to leave the pool.
         * @return Nothing.
         */
        void file_mapping_record_pool_remove_page_slots(FileMappingRecordPool* pool, const FileMappingRecordPageHeader* retired_page) {
            FileMappingRecordSlot* node;
            FileMappingRecordSlot* retained_head = NULL;

            if ((pool == NULL) || (retired_page == NULL)) {
                return;
            }

            node = pool->free_list;
            while (node != NULL) {
                FileMappingRecordSlot* next = node->next_free;

                if (file_mapping_record_page_from_slot(node) != retired_page) {
                    node->next_free = retained_head;
                    retained_head = node;
                }
                node = next;
            }

            pool->free_list = retained_head;
        }

        /**
         * Unlink one metadata page from the owning pool page chain.
         *
         * @param pool Pool that owns the page.
         * @param page_header Page to unlink.
         * @return Nothing.
         */
        void file_mapping_record_pool_unlink_page(FileMappingRecordPool* pool, FileMappingRecordPageHeader* page_header) {
            FileMappingRecordPageHeader* current;
            FileMappingRecordPageHeader* previous = NULL;

            if ((pool == NULL) || (page_header == NULL)) {
                return;
            }

            current = pool->pages;
            while (current != NULL) {
                if (current == page_header) {
                    if (previous != NULL) {
                        previous->next_page = current->next_page;
                    }
                    else {
                        pool->pages = current->next_page;
                    }
                    return;
                }

                previous = current;
                current = current->next_page;
            }
        }

        /**
         * Return one metadata record slot to its page-backed pool.
         *
         * @param pool Pool receiving the record.
         * @param record Slot pointer previously returned by the pool.
         * @return Nothing.
         */
        void file_mapping_record_pool_free(FileMappingRecordPool* pool, void* record) {
            FileMappingRecordSlot* slot;
            FileMappingRecordPageHeader* page_header;

            if ((pool == NULL) || (record == NULL)) {
                return;
            }

            slot = reinterpret_cast<FileMappingRecordSlot*>(record);
            page_header = file_mapping_record_page_from_slot(slot);
            if ((page_header == NULL) || (page_header->live_slots == 0U)) {
                return;
            }

            --page_header->live_slots;
            slot->next_free = pool->free_list;
            pool->free_list = slot;
            if (page_header->live_slots != 0U) {
                return;
            }

            file_mapping_record_pool_remove_page_slots(pool, page_header);
            file_mapping_record_pool_unlink_page(pool, page_header);
            mm::PhysicalMemory::free_page(page_header->page_phys);
        }

        /**
         * Release one page list previously allocated for a file mapping object.
         *
         * @param pages Page array to release.
         * @param page_count Number of entries stored in the page array.
         * @return Nothing.
         */
        void release_backing_pages(PhysAddr* pages, Size page_count) {
            if (pages == NULL) {
                return;
            }

            for (Size index = 0U; index < page_count; ++index) {
                if (pages[index] != 0U) {
                    mm::PhysicalMemory::free_page(pages[index]);
                }
            }

            Heap::free(pages);
        }

        /**
         * Map one run of backing pages into one process address space.
         *
         * The file-mapping metadata stays pointer-free in the mapped region by
         * keeping the physical page list in kernel heap memory and mapping the
         * view one page at a time.
         *
         * @param process Target process that owns the address space.
         * @param view_address Base address for the mapped view.
         * @param backing_pages Page list that backs the object.
         * @param page_offset First page inside the object to map.
         * @param page_count Number of pages to map.
         * @return StatusOK on success.
         */
        Status map_backing_pages(Process* process, VirtAddr view_address, const PhysAddr* backing_pages, Size page_offset, Size page_count) {
            VmMapping mapping = {};

            if ((process == NULL) || (backing_pages == NULL) || (page_count == 0U)) {
                return StatusInvalidArgument;
            }

            for (Size index = 0U; index < page_count; ++index) {
                mapping.virtual_base = view_address + ((page_offset + index) * mm::PageSize);
                mapping.physical_base = backing_pages[page_offset + index];
                mapping.length = mm::PageSize;
                mapping.flags = PagePresent | PageWritable | PageUser;

                Status status = mm::MemoryManager::map(&process->process_address_space, &mapping);
                if (status != StatusOK) {
                    if (index != 0U) {
                        (void)mm::MemoryManager::unmap(
                            &process->process_address_space,
                            view_address + (page_offset * mm::PageSize),
                            index * mm::PageSize);
                    }
                    return status;
                }
            }

            return StatusOK;
        }

        /**
         * Unmap one run of view pages from one process.
         *
         * @param process Process that owns the mapped view.
         * @param view_address Base address for the mapped view.
         * @param offset_bytes Offset from the view base where the range begins.
         * @param bytes Number of bytes to unmap.
         * @return Nothing.
         */
        void unmap_backing_pages(Process* process, VirtAddr view_address, Size offset_bytes, Size bytes) {
            if ((process == NULL) || (bytes == 0U) || (process->process_address_space.page_table_root == 0U)) {
                return;
            }

            (void)mm::MemoryManager::unmap(&process->process_address_space, view_address + offset_bytes, bytes);
        }

        /**
         * Return the base address for one reserved file-mapping slot.
         *
         * @param slot_index Slot number inside the file-mapping window.
         * @return User virtual address for the slot.
         */
        VirtAddr slot_address(U32 slot_index) {
            return user_address_space::FileMappingViewBase + (static_cast<VirtAddr>(slot_index) * user_address_space::FileMappingViewSlotBytes);
        }

        /**
         * Report whether one process already owns the requested file-mapping slot.
         *
         * File mappings keep stable EL0 base addresses for the lifetime of one
         * attachment, so every new view must claim one unused L2 slot inside
         * the file-mapping reservation below the heap.
         *
         * @param process Process receiving or owning the mapping.
         * @param slot_index Candidate slot number.
         * @return True when the slot is already occupied inside the process.
         */
        bool process_uses_slot(Process* process, U32 slot_index) {
            for (const FileMappingAttachment* attachment = g_file_mapping_attachment_head; attachment != NULL; attachment = attachment->next) {
                if (attachment->in_use && (attachment->process == process) && (attachment->slot_index == slot_index)) {
                    return true;
                }
            }

            return false;
        }

        /**
         * Reserve one free file-mapping slot inside the full EL0 view arena.
         *
         * The arena size now comes from the address-layout gap below the heap
         * instead of a small hard-coded slot count, so this scan derives the
         * usable slot capacity from the reserved byte range.
         *
         * @param process Process receiving the mapping.
         * @param slot_index_out Receives the reserved slot index.
         * @return StatusOK on success, or StatusNoSpace when the arena is full.
         */
        Status reserve_view_slot(Process* process, U32* slot_index_out) {
            const Size slot_capacity = user_address_space::FileMappingViewBytes / user_address_space::FileMappingViewSlotBytes;

            if ((process == NULL) || (slot_index_out == NULL)) {
                return StatusInvalidArgument;
            }

            for (Size slot_index = 0U; slot_index < slot_capacity; ++slot_index) {
                if (!process_uses_slot(process, static_cast<U32>(slot_index))) {
                    *slot_index_out = static_cast<U32>(slot_index);
                    return StatusOK;
                }
            }

            return StatusNoSpace;
        }

        /**
         * Initialize the registry once.
         *
         * @return StatusOK after the registry is ready.
         */
        Status init_registry(void) {
            if (!g_file_mapping_initialized) {
                file_mapping_record_pool_init(&g_file_mapping_object_pool, sizeof(FileMappingObject), alignof(FileMappingObject));
                file_mapping_record_pool_init(&g_file_mapping_attachment_pool, sizeof(FileMappingAttachment), alignof(FileMappingAttachment));
                g_file_mapping_head = NULL;
                g_file_mapping_tail = NULL;
                g_file_mapping_attachment_head = NULL;
                g_file_mapping_attachment_tail = NULL;
                g_file_mapping_next_handle = 1ULL;
                g_file_mapping_initialized = true;
            }

            return StatusOK;
        }

        /**
         * Find one object by handle value.
         *
         * @param handle Handle value to search.
         * @return Matching object, or NULL.
         */
        FileMappingObject* find_object_by_handle(FileMappingId handle) {
            for (FileMappingObject* object = g_file_mapping_head; object != NULL; object = object->next) {
                if (object->in_use && (object->handle_value == handle)) {
                    return object;
                }
            }

            return NULL;
        }

        /**
         * Find one object by path.
         *
         * @param file_path Namespace key.
         * @return Matching object, or NULL.
         */
        FileMappingObject* find_object_by_path(const char* file_path) {
            for (FileMappingObject* object = g_file_mapping_head; object != NULL; object = object->next) {
                if (object->in_use && same_text_case_insensitive(object->file_path, file_path)) {
                    return object;
                }
            }

            return NULL;
        }

        /**
         * Find one process-local attachment for a mapping object.
         *
         * @param process Owning process.
         * @param object Mapping object.
         * @return Existing attachment, or NULL.
         */
        FileMappingAttachment* find_attachment(Process* process, FileMappingObject* object) {
            for (FileMappingAttachment* attachment = g_file_mapping_attachment_head; attachment != NULL; attachment = attachment->next) {
                if (attachment->in_use && (attachment->process == process) && (attachment->object == object)) {
                    return attachment;
                }
            }

            return NULL;
        }

        /**
         * Find one attachment by its mapped base address.
         *
         * @param process Owning process.
         * @param base_address Mapped view base.
         * @return Matching attachment, or NULL.
         */
        FileMappingAttachment* find_attachment_by_view(Process* process, VirtAddr base_address) {
            for (FileMappingAttachment* attachment = g_file_mapping_attachment_head; attachment != NULL; attachment = attachment->next) {
                if (attachment->in_use && (attachment->process == process) && (attachment->view_address == base_address)) {
                    return attachment;
                }
            }

            return NULL;
        }

        /**
         * Link one object into the intrusive registry.
         *
         * @param object Object to publish.
         * @return Nothing.
         */
        void link_object(FileMappingObject* object) {
            if (object == NULL) {
                return;
            }

            object->prev = g_file_mapping_tail;
            object->next = NULL;
            if (g_file_mapping_tail != NULL) {
                g_file_mapping_tail->next = object;
            }
            else {
                g_file_mapping_head = object;
            }

            g_file_mapping_tail = object;
        }

        /**
         * Link one attachment into the intrusive registry.
         *
         * @param attachment Attachment to publish.
         * @return Nothing.
         */
        void link_attachment(FileMappingAttachment* attachment) {
            if (attachment == NULL) {
                return;
            }

            attachment->prev = g_file_mapping_attachment_tail;
            attachment->next = NULL;
            if (g_file_mapping_attachment_tail != NULL) {
                g_file_mapping_attachment_tail->next = attachment;
            }
            else {
                g_file_mapping_attachment_head = attachment;
            }

            g_file_mapping_attachment_tail = attachment;
        }

        /**
         * Unlink one object from the intrusive registry.
         *
         * @param object Object to remove.
         * @return Nothing.
         */
        void unlink_object(FileMappingObject* object) {
            if (object == NULL) {
                return;
            }

            if (object->prev != NULL) {
                object->prev->next = object->next;
            }
            else {
                g_file_mapping_head = object->next;
            }
            if (object->next != NULL) {
                object->next->prev = object->prev;
            }
            else {
                g_file_mapping_tail = object->prev;
            }

            object->next = NULL;
            object->prev = NULL;
        }

        /**
         * Unlink one attachment from the intrusive registry.
         *
         * @param attachment Attachment to remove.
         * @return Nothing.
         */
        void unlink_attachment(FileMappingAttachment* attachment) {
            if (attachment == NULL) {
                return;
            }

            if (attachment->prev != NULL) {
                attachment->prev->next = attachment->next;
            }
            else {
                g_file_mapping_attachment_head = attachment->next;
            }
            if (attachment->next != NULL) {
                attachment->next->prev = attachment->prev;
            }
            else {
                g_file_mapping_attachment_tail = attachment->prev;
            }

            attachment->next = NULL;
            attachment->prev = NULL;
        }

        /**
         * Allocate the physical backing for one mapping object.
         *
         * The backing lives as a kernel-owned page list so the object can grow
         * without depending on one contiguous physical run.
         *
         * @param object Mapping object to populate.
         * @param size Requested file size.
         * @return StatusOK on success.
         */
        Status allocate_backing(FileMappingObject* object, Size size) {
            const Size rounded_bytes = align_up_to_page(size);
            const Size page_count = rounded_bytes / mm::PageSize;
            PhysAddr* backing_pages;

            if ((object == NULL) || (size == 0U) || (rounded_bytes == 0U) || (page_count == 0U)) {
                return StatusInvalidArgument;
            }

            backing_pages = static_cast<PhysAddr*>(Heap::alloc(page_count * sizeof(PhysAddr), alignof(PhysAddr)));
            if (backing_pages == NULL) {
                return StatusNoMemory;
            }

            for (Size index = 0U; index < page_count; ++index) {
                PhysAddr page_phys = mm::PhysicalMemory::alloc_page();

                if (page_phys == 0U) {
                    release_backing_pages(backing_pages, index);
                    return StatusNoMemory;
                }

                backing_pages[index] = page_phys;
            }

            object->backing_pages = backing_pages;
            object->backing_page_count = page_count;
            object->backing_bytes = rounded_bytes;
            object->requested_size = size;
            object->dirty = false;
            return StatusOK;
        }

        /**
         * Grow one live file-mapping object to a larger logical size.
         *
         * Resize stays grow-only so the backing list never has to be compacted
         * or relocated out from underneath existing attachments.
         *
         * @param object Mapping object to extend.
         * @param size Requested logical size after the resize.
         * @return StatusOK on success.
         */
        Status resize_backing(FileMappingObject* object, Size size) {
            const Size new_backing_bytes = align_up_to_page(size);
            const Size old_backing_bytes = (object != NULL) ? object->backing_bytes : 0U;
            const Size old_page_count = (object != NULL) ? object->backing_page_count : 0U;
            const Size new_page_count = new_backing_bytes / mm::PageSize;
            PhysAddr* resized_pages;

            if ((object == NULL) || (size == 0U)) {
                return StatusInvalidArgument;
            }
            if (size < object->requested_size) {
                return StatusNotSupported;
            }
            if (new_backing_bytes == 0U) {
                return StatusInvalidArgument;
            }
            if (new_backing_bytes > user_address_space::FileMappingViewSlotBytes) {
                return StatusNoSpace;
            }
            if (new_backing_bytes <= old_backing_bytes) {
                object->requested_size = size;
                return StatusOK;
            }

            resized_pages = static_cast<PhysAddr*>(Heap::alloc(new_page_count * sizeof(PhysAddr), alignof(PhysAddr)));
            if (resized_pages == NULL) {
                return StatusNoMemory;
            }

            if ((old_page_count != 0U) && (object->backing_pages != NULL)) {
                memcopy(resized_pages, object->backing_pages, old_page_count * sizeof(PhysAddr));
            }

            for (Size index = old_page_count; index < new_page_count; ++index) {
                PhysAddr page_phys = mm::PhysicalMemory::alloc_page();

                if (page_phys == 0U) {
                    release_backing_pages(resized_pages, index);
                    return StatusNoMemory;
                }

                resized_pages[index] = page_phys;
            }

            if (object->attachment_count != 0U) {
                const Size appended_pages = new_page_count - old_page_count;
                const Size appended_bytes = appended_pages * mm::PageSize;

                for (FileMappingAttachment* attachment = g_file_mapping_attachment_head; attachment != NULL; attachment = attachment->next) {
                    if (!attachment->in_use || (attachment->object != object) || (attachment->view_bytes != old_backing_bytes)) {
                        continue;
                    }
                    if ((attachment->process == NULL) || (attachment->process->process_address_space.page_table_root == 0U)) {
                        continue;
                    }

                    Status status = map_backing_pages(
                        attachment->process,
                        attachment->view_address,
                        resized_pages,
                        old_page_count,
                        appended_pages);
                    if (status != StatusOK) {
                        for (FileMappingAttachment* rollback = g_file_mapping_attachment_head; rollback != NULL; rollback = rollback->next) {
                            if (!rollback->in_use || (rollback->object != object) || (rollback->view_bytes != new_backing_bytes)) {
                                continue;
                            }
                            if ((rollback->process == NULL) || (rollback->process->process_address_space.page_table_root == 0U)) {
                                continue;
                            }

                            unmap_backing_pages(
                                rollback->process,
                                rollback->view_address,
                                old_backing_bytes,
                                appended_bytes);
                            rollback->view_bytes = old_backing_bytes;
                        }
                        for (Size rollback_page = old_page_count; rollback_page < new_page_count; ++rollback_page) {
                            mm::PhysicalMemory::free_page(resized_pages[rollback_page]);
                        }
                        Heap::free(resized_pages);
                        return status;
                    }

                    attachment->view_bytes = new_backing_bytes;
                }
            }

            release_backing_pages(object->backing_pages, object->backing_page_count);
            object->backing_pages = resized_pages;
            object->backing_page_count = new_page_count;
            object->backing_bytes = new_backing_bytes;
            object->requested_size = size;
            object->dirty = false;
            return StatusOK;
        }

        /**
         * Release one object when no handle and no view still references it.
         *
         * @param object Object candidate for destruction.
         * @return Nothing.
         */
        void release_object_if_unused(FileMappingObject* object) {
            if ((object == NULL) || !object->in_use || (object->handle_count != 0U) || (object->attachment_count != 0U)) {
                return;
            }

            release_backing_pages(object->backing_pages, object->backing_page_count);

            unlink_object(object);
            memzero(object, sizeof(*object));
            file_mapping_record_pool_free(&g_file_mapping_object_pool, object);
        }

        /**
         * Release one attachment and optionally unmap it from the process.
         *
         * @param attachment Attachment to release.
         * @param process_alive True when the process address space is still valid.
         * @return StatusOK on success.
         */
        Status release_attachment(FileMappingAttachment* attachment, bool process_alive) {
            FileMappingObject* object;

            if ((attachment == NULL) || !attachment->in_use || (attachment->process == NULL) || (attachment->object == NULL)) {
                return StatusInvalidArgument;
            }

            object = attachment->object;
            if (process_alive && (attachment->process->process_address_space.page_table_root != 0U)) {
                (void)mm::MemoryManager::unmap(&attachment->process->process_address_space, attachment->view_address, attachment->view_bytes);
            }

            if (object->attachment_count != 0U) {
                --object->attachment_count;
            }

            unlink_attachment(attachment);
            memzero(attachment, sizeof(*attachment));
            file_mapping_record_pool_free(&g_file_mapping_attachment_pool, attachment);
            release_object_if_unused(object);
            return StatusOK;
        }

        /**
         * Create or open one mapping object using the fixed path namespace.
         *
         * @param file_path Namespace key.
         * @param size Requested size in bytes.
         * @param handle_out Receives the file-mapping handle.
         * @param create_when_missing True when a new object may be created.
         * @return StatusOK on success.
         */
        Status open_or_create(const char* file_path, unsigned long size, FileMappingId* handle_out, bool create_when_missing) {
            FileMappingObject* object;
            Status status;
            const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

            if ((file_path == NULL) || (file_path[0] == '\0') || (handle_out == NULL) || (size == 0UL)) {
                arch::Arch::restore_interrupts(interrupts_enabled);
                return StatusInvalidArgument;
            }

            status = init_registry();
            if (status != StatusOK) {
                arch::Arch::restore_interrupts(interrupts_enabled);
                return status;
            }

            object = find_object_by_path(file_path);
            if (object != NULL) {
                if (create_when_missing) {
                    arch::Arch::restore_interrupts(interrupts_enabled);
                    return StatusAlreadyExists;
                }
                if (static_cast<Size>(size) > object->requested_size) {
                    arch::Arch::restore_interrupts(interrupts_enabled);
                    return StatusNoSpace;
                }

                ++object->handle_count;
                *handle_out = object->handle_value;
                arch::Arch::restore_interrupts(interrupts_enabled);
                return StatusOK;
            }

            if (!create_when_missing) {
                arch::Arch::restore_interrupts(interrupts_enabled);
                return StatusNotFound;
            }

            if (static_cast<Size>(size) > user_address_space::FileMappingViewSlotBytes) {
                arch::Arch::restore_interrupts(interrupts_enabled);
                return StatusNoSpace;
            }

            FileMappingObject* candidate = static_cast<FileMappingObject*>(file_mapping_record_pool_allocate(&g_file_mapping_object_pool));

            if (candidate == NULL) {
                arch::Arch::restore_interrupts(interrupts_enabled);
                return StatusNoMemory;
            }

            candidate->in_use = true;
            candidate->handle_value = g_file_mapping_next_handle++;
            candidate->handle_count = 1U;
            candidate->attachment_count = 0U;
            candidate->requested_size = static_cast<Size>(size);
            copy_path(candidate->file_path, sizeof(candidate->file_path), file_path);

            status = allocate_backing(candidate, static_cast<Size>(size));
            if (status != StatusOK) {
                memzero(candidate, sizeof(*candidate));
                file_mapping_record_pool_free(&g_file_mapping_object_pool, candidate);
                arch::Arch::restore_interrupts(interrupts_enabled);
                return status;
            }

            link_object(candidate);
            *handle_out = candidate->handle_value;
            arch::Arch::restore_interrupts(interrupts_enabled);
            return StatusOK;
        }
    } // namespace

    Status CreateFileMapping(const char* filePath, unsigned long size, FileMappingId* handleOut) {
        return open_or_create(filePath, size, handleOut, true);
    }

    Status OpenFileMapping(const char* filePath, unsigned long size, FileMappingId* handleOut) {
        return open_or_create(filePath, size, handleOut, false);
    }

    Status CloseFileMapping(FileMappingId mappingHandle) {
        FileMappingObject* object;
        Status status;
        const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

        status = init_registry();
        if (status != StatusOK) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return status;
        }
        object = find_object_by_handle(mappingHandle);
        if (object == NULL) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return StatusNotFound;
        }
        if (object->handle_count == 0U) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return StatusNotFound;
        }

        --object->handle_count;
        release_object_if_unused(object);
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusOK;
    }

    Status MapViewOfFile(Process* process, FileMappingId mappingHandle, unsigned long offset, unsigned long size, void** addressOut) {
        FileMappingObject* object;
        FileMappingAttachment* attachment;
        U32 slot_index;
        Size mapped_bytes;
        Status status;
        const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

        if ((process == NULL) || (addressOut == NULL) || (size == 0UL)) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return StatusInvalidArgument;
        }
        if ((offset % mm::PageSize) != 0U) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return StatusInvalidArgument;
        }

        mapped_bytes = align_up_to_page(static_cast<Size>(size));
        if ((mapped_bytes == 0U) || (mapped_bytes > user_address_space::FileMappingViewSlotBytes)) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return StatusNoSpace;
        }

        status = init_registry();
        if (status != StatusOK) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return status;
        }
        object = find_object_by_handle(mappingHandle);
        if ((object == NULL) || (object->handle_count == 0U) || (object->backing_pages == NULL)) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return StatusNotFound;
        }
        if ((static_cast<Size>(offset) >= object->backing_bytes) || (mapped_bytes > (object->backing_bytes - static_cast<Size>(offset)))) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return StatusInvalidArgument;
        }

        attachment = find_attachment(process, object);
        if (attachment != NULL) {
            *addressOut = reinterpret_cast<void*>(attachment->view_address);
            arch::Arch::restore_interrupts(interrupts_enabled);
            return StatusOK;
        }

        status = reserve_view_slot(process, &slot_index);
        if (status != StatusOK) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return status;
        }

        attachment = static_cast<FileMappingAttachment*>(file_mapping_record_pool_allocate(&g_file_mapping_attachment_pool));
        if (attachment == NULL) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return StatusNoMemory;
        }

        attachment->in_use = true;
        attachment->slot_index = slot_index;
        attachment->process = process;
        attachment->object = object;
        attachment->view_address = slot_address(slot_index);
        attachment->view_bytes = mapped_bytes;

        status = map_backing_pages(
            process,
            attachment->view_address,
            object->backing_pages,
            static_cast<Size>(offset / mm::PageSize),
            static_cast<Size>(mapped_bytes / mm::PageSize));
        if (status != StatusOK) {
            memzero(attachment, sizeof(*attachment));
            file_mapping_record_pool_free(&g_file_mapping_attachment_pool, attachment);
            arch::Arch::restore_interrupts(interrupts_enabled);
            return status;
        }

        link_attachment(attachment);
        ++object->attachment_count;
        *addressOut = reinterpret_cast<void*>(attachment->view_address);
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusOK;
    }

    Status ResizeFileMapping(FileMappingId mappingHandle, unsigned long size) {
        FileMappingObject* object;
        Status status;
        const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

        if (size == 0UL) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return StatusInvalidArgument;
        }

        status = init_registry();
        if (status != StatusOK) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return status;
        }

        object = find_object_by_handle(mappingHandle);
        if ((object == NULL) || (object->handle_count == 0U)) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return StatusNotFound;
        }

        status = resize_backing(object, static_cast<Size>(size));
        arch::Arch::restore_interrupts(interrupts_enabled);
        return status;
    }

    Status UnmapViewOfFile(Process* process, void* baseAddress) {
        FileMappingAttachment* attachment;
        Status status;
        const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

        if ((process == NULL) || (baseAddress == NULL)) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return StatusInvalidArgument;
        }

        status = init_registry();
        if (status != StatusOK) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return status;
        }
        attachment = find_attachment_by_view(process, reinterpret_cast<VirtAddr>(baseAddress));
        if (attachment == NULL) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return StatusNotFound;
        }

        (void)release_attachment(attachment, process->process_address_space.page_table_root != 0U);
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusOK;
    }

    Status release_process_mappings(Process* process) {
        FileMappingAttachment* attachment;
        FileMappingAttachment* next;
        Status status;
        const bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

        if (process == NULL) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return StatusInvalidArgument;
        }

        status = init_registry();
        if (status != StatusOK) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return status;
        }
        attachment = g_file_mapping_attachment_head;
        while (attachment != NULL) {
            next = attachment->next;
            if (attachment->in_use && (attachment->process == process)) {
                (void)release_attachment(attachment, process->process_address_space.page_table_root != 0U);
            }
            attachment = next;
        }

        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusOK;
    }

} // namespace FileMapping