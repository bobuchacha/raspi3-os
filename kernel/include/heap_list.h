#ifndef KERNEL_INCLUDE_HEAP_LIST_H
#define KERNEL_INCLUDE_HEAP_LIST_H

#include "heap.h"

/**
 * Minimal heap-backed dynamic array for trivially copyable kernel types.
 *
 * The kernel avoids the hosted C++ standard library, but several registries now
 * need growable storage after heap initialization. This container intentionally
 * stays memcpy-friendly so it can back pointer lists and POD-style records
 * without pulling in exception support or allocator traits.
 */
template <typename T>
class HeapList final {
public:
    /**
     * Reserve space for at least the requested number of elements.
     *
     * @param requested_count Minimum element capacity to keep.
     * @return StatusOK on success, or StatusNoMemory when growth fails.
     */
    Status reserve(Size requested_count) {
        T* grown_items;
        Size grown_capacity;

        if (requested_count <= m_capacity) {
            return StatusOK;
        }

        grown_capacity = (m_capacity == 0U) ? 4U : m_capacity;
        while (grown_capacity < requested_count) {
            if (grown_capacity > (~static_cast<Size>(0U) / 2U)) {
                grown_capacity = requested_count;
                break;
            }

            grown_capacity *= 2U;
        }

        grown_items = static_cast<T*>(Heap::realloc(m_items, grown_capacity * sizeof(T)));
        if (grown_items == NULL) {
            return StatusNoMemory;
        }

        m_items = grown_items;
        m_capacity = grown_capacity;
        return StatusOK;
    }

    /**
     * Append one new element to the tail of the list.
     *
     * @param value Value to append.
     * @return StatusOK on success, or a propagated growth error.
     */
    Status append(const T& value) {
        Status status = reserve(m_count + 1U);

        if (status != StatusOK) {
            return status;
        }

        m_items[m_count++] = value;
        return StatusOK;
    }

    /**
     * Remove one element while preserving the order of later entries.
     *
     * @param index Zero-based element index to remove.
     * @return Nothing.
     */
    void remove_at(Size index) {
        if (index >= m_count) {
            return;
        }

        for (Size shift = index + 1U; shift < m_count; ++shift) {
            m_items[shift - 1U] = m_items[shift];
        }

        --m_count;
    }

    /**
     * Release the backing storage and reset the list to empty.
     *
     * @return Nothing.
     */
    void clear(void) {
        if (m_items != NULL) {
            Heap::free(m_items);
        }

        m_items = NULL;
        m_count = 0U;
        m_capacity = 0U;
    }

    /**
     * Report how many elements are currently stored.
     *
     * @return Active element count.
     */
    Size count(void) const {
        return m_count;
    }

    /**
     * Expose indexed access to one stored element.
     *
     * @param index Zero-based element index.
     * @return Mutable element reference.
     */
    T& operator[](Size index) {
        return m_items[index];
    }

    /**
     * Expose indexed access to one stored element.
     *
     * @param index Zero-based element index.
     * @return Const element reference.
     */
    const T& operator[](Size index) const {
        return m_items[index];
    }

private:
    T* m_items = NULL;
    Size m_count = 0U;
    Size m_capacity = 0U;
};

#endif // KERNEL_INCLUDE_HEAP_LIST_H