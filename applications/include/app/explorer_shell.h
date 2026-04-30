#ifndef ROS_APP_EXPLORER_SHELL_H
#define ROS_APP_EXPLORER_SHELL_H

#include "window.h"

#define ROS_EXPLORER_SHELL_SHARED_STATE_NAME "ros.explorer.shell.state"
#define ROS_EXPLORER_SHELL_SHARED_STATE_VERSION 3U
#define ROS_EXPLORER_SHELL_TASK_CAPACITY_DEFAULT 128U

#define ROS_EXPLORER_SHELL_IPC_PROTOCOL 0x58504C52UL
#define ROS_EXPLORER_SHELL_IPC_KIND_DESKTOP_CLICK 1UL

#define ROS_EXPLORER_SHELL_TASK_FLAG_VISIBLE (1U << 0)
#define ROS_EXPLORER_SHELL_TASK_FLAG_ACTIVE (1U << 1)
#define ROS_EXPLORER_SHELL_TASK_FLAG_DECORATED (1U << 2)
#define ROS_EXPLORER_SHELL_TASK_FLAG_TOPMOST (1U << 3)
#define ROS_EXPLORER_SHELL_TASK_FLAG_FULLSCREEN (1U << 4)

typedef struct RosExplorerShellTaskEntryStruct {
    uint64_t hwnd;
    int64_t owner_pid;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t style;
    uint32_t flags;
    char class_name[ROS_WINDOW_CLASS_NAME_MAX];
    char title[ROS_WINDOW_TITLE_MAX];
} RosExplorerShellTaskEntry;

typedef struct RosExplorerShellSharedStateStruct {
    uint32_t version;
    uint32_t task_capacity;
    uint64_t task_generation;
    uint64_t shell_generation;
    int64_t shell_pid;
    uint64_t taskbar_hwnd;
    uint64_t start_menu_hwnd;
    uint64_t foreground_hwnd;
    uint64_t focused_hwnd;
    uint64_t fullscreen_hwnd;
    uint32_t desktop_width;
    uint32_t desktop_height;
    int32_t work_area_x;
    int32_t work_area_y;
    uint32_t work_area_width;
    uint32_t work_area_height;
    uint32_t taskbar_visible;
    uint32_t taskbar_height;
    uint32_t start_menu_visible;
    uint32_t task_count;
    RosExplorerShellTaskEntry tasks[1];
} RosExplorerShellSharedState;

/*
 * Return the byte size required for one shell-state block with the requested task capacity.
 *
 * The shell snapshot lives in named shared memory, so the producer and all
 * consumers must derive the exact mapped byte length from one helper instead of
 * duplicating a fixed trailing-array size in multiple translation units.
 *
 * @param task_capacity Number of task entries the mapping should hold.
 * @return Byte size for the full shared-memory object, or zero when invalid.
 */
static inline unsigned long ExplorerShellSharedStateBytes(unsigned long task_capacity) {
    if (task_capacity == 0UL) {
        return 0UL;
    }

    return (unsigned long)(sizeof(RosExplorerShellSharedState)
        + (sizeof(RosExplorerShellTaskEntry) * (task_capacity - 1UL)));
}

/*
 * Return the published task capacity for one mapped shell-state block.
 *
 * @param state Shared-state mapping to query.
 * @return Published task capacity, or zero when the mapping is invalid.
 */
static inline unsigned long ExplorerShellTaskCapacity(const RosExplorerShellSharedState* state) {
    if (state == 0 || state->task_capacity == 0U) {
        return 0UL;
    }

    return (unsigned long)state->task_capacity;
}

/*
 * Map the shared explorer shell state block.
 *
 * GWES creates the region on startup and explorer opens it read-write so the
 * shell and window server can exchange task snapshots and work-area policy
 * without adding another syscall family.
 *
 * @param requested_size Non-zero to create the region, or zero to require an existing one.
 * @return Mapped shared-state pointer, or null when the mapping failed.
 */
static inline RosExplorerShellSharedState* ExplorerShellSharedState(unsigned long requested_size) {
    void* address = 0;

    if (acquireSharedMemoryRegion(ROS_EXPLORER_SHELL_SHARED_STATE_NAME, requested_size, &address) < 0) {
        return 0;
    }

    return (RosExplorerShellSharedState*)address;
}

/*
 * Initialize one freshly created shell-state block.
 *
 * The version and capacity fields let explorer reject stale mappings and keep
 * the shared ABI explicit as the taskbar contract evolves.
 *
 * @param state Shared-state mapping to initialize.
 * @return Nothing.
 */
static inline void ExplorerShellInitializeSharedState(RosExplorerShellSharedState* state) {
    unsigned long task_capacity;

    if (!state) {
        return;
    }

    task_capacity = ExplorerShellTaskCapacity(state);
    if (task_capacity == 0UL) {
        task_capacity = ROS_EXPLORER_SHELL_TASK_CAPACITY_DEFAULT;
    }

    state->version = ROS_EXPLORER_SHELL_SHARED_STATE_VERSION;
    state->task_capacity = (uint32_t)task_capacity;
    state->task_generation = 0ULL;
    state->shell_generation = 0ULL;
    state->shell_pid = ROS_USER_IPC_STATUS_NOT_FOUND;
    state->taskbar_hwnd = 0ULL;
    state->start_menu_hwnd = 0ULL;
    state->foreground_hwnd = 0ULL;
    state->focused_hwnd = 0ULL;
    state->fullscreen_hwnd = 0ULL;
    state->desktop_width = 0U;
    state->desktop_height = 0U;
    state->work_area_x = 0;
    state->work_area_y = 0;
    state->work_area_width = 0U;
    state->work_area_height = 0U;
    state->taskbar_visible = 0U;
    state->taskbar_height = 0U;
    state->start_menu_visible = 0U;
    state->task_count = 0U;
    for (unsigned long index = 0UL; index < task_capacity; ++index) {
        state->tasks[index].hwnd = 0ULL;
        state->tasks[index].owner_pid = 0LL;
        state->tasks[index].x = 0;
        state->tasks[index].y = 0;
        state->tasks[index].width = 0U;
        state->tasks[index].height = 0U;
        state->tasks[index].style = 0U;
        state->tasks[index].flags = 0U;
        state->tasks[index].class_name[0] = '\0';
        state->tasks[index].title[0] = '\0';
    }
}

#endif