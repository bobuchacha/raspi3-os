#include "user_runtime.h"
#include "app/windowkit.h"

#define TASKMAN_REFRESH_MSEC 120UL
#define TASKMAN_BACKGROUND 0x00101924U
#define TASKMAN_TASK_MAX 24U
#define TASKMAN_PROGRAM_MAX 16U
#define TASKMAN_LIBRARY_MAX 8U
#define TASKMAN_DRIVER_MAX 8U
#define TASKMAN_NAME_MAX 128U

typedef enum TaskmanWindowId {
    TaskmanWindowTasks = 0,
    TaskmanWindowDetails = 1,
    TaskmanWindowPrograms = 2,
    TaskmanWindowStorage = 3,
    TaskmanWindowCount = 4,
} TaskmanWindowId;

typedef struct TaskmanWindowStateStruct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t accentColor;
    const char* title;
} TaskmanWindowState;

typedef struct TaskmanTaskEntryStruct {
    long pid;
    UserTaskInfo info;
} TaskmanTaskEntry;

typedef struct TaskmanPathEntryStruct {
    char name[TASKMAN_NAME_MAX];
    char path[TASKMAN_NAME_MAX];
} TaskmanPathEntry;

typedef struct TaskmanAppStateStruct {
    TaskmanWindowState windows[TaskmanWindowCount];
    TaskmanWindowId order[TaskmanWindowCount];
    TaskmanTaskEntry tasks[TASKMAN_TASK_MAX];
    TaskmanPathEntry programs[TASKMAN_PROGRAM_MAX];
    TaskmanPathEntry libraries[TASKMAN_LIBRARY_MAX];
    TaskmanPathEntry drivers[TASKMAN_DRIVER_MAX];
    UserMemInfo memInfo;
    WindowKitPointerState pointer;
    WindowKitPointerState lastPointer;
    unsigned int taskCount;
    unsigned int programCount;
    unsigned int libraryCount;
    unsigned int driverCount;
    unsigned int selectedTask;
    unsigned int selectedProgram;
    unsigned int runningCount;
    unsigned int readyCount;
    unsigned int sleepingCount;
    unsigned int blockedCount;
    int dragWindow;
    int dragOffsetX;
    int dragOffsetY;
} TaskmanAppState;

static TaskmanAppState g_taskmanState;
static WindowKitFrame g_taskmanFrame;

static int taskmanTextEquals(const char* left, const char* right) {
    if (!left || !right) {
        return 0;
    }

    while (*left != '\0' && *right != '\0' && *left == *right) {
        left++;
        right++;
    }

    return *left == *right;
}

static int taskmanTextEndsWith(const char* text, const char* suffix) {
    unsigned long textLen = textLength(text);
    unsigned long suffixLen = textLength(suffix);

    if (!text || !suffix || suffixLen > textLen) {
        return 0;
    }

    return taskmanTextEquals(text + (textLen - suffixLen), suffix);
}

static void taskmanCopyText(char* destination, unsigned long size, const char* text) {
    unsigned long index = 0UL;

    if (!destination || size == 0UL) {
        return;
    }
    if (!text) {
        destination[0] = '\0';
        return;
    }

    while (index + 1UL < size && text[index] != '\0') {
        destination[index] = text[index];
        index++;
    }
    destination[index] = '\0';
}

static const char* taskmanStateName(long state) {
    switch (state) {
    case 0:
        return "zombie";
    case 1:
        return "ready";
    case 2:
        return "run";
    case 3:
        return "sleep";
    case 4:
        return "block";
    default:
        return "unknown";
    }
}

static void taskmanBuildProgramName(const char* path, char* name, unsigned long size) {
    const char* cursor = path;
    const char* base = path;
    unsigned long index = 0UL;

    if (!name || size == 0UL) {
        return;
    }
    if (!path || path[0] == '\0') {
        taskmanCopyText(name, size, "app");
        return;
    }

    while (*cursor != '\0') {
        if (*cursor == '/') {
            base = cursor + 1;
        }
        cursor++;
    }

    while (base[index] != '\0' && base[index] != '.' && index + 1UL < size) {
        name[index] = base[index];
        index++;
    }
    name[index] = '\0';
    if (index == 0UL) {
        taskmanCopyText(name, size, "app");
    }
}

static void taskmanBuildUnsignedLine(char* line, const char* label, unsigned long value) {
    char* cursor = line;

    cursor = appendText(cursor, label);
    cursor = appendUnsignedLong(cursor, value);
    *cursor = '\0';
}

static void taskmanBuildTaskLine(char* line, const TaskmanTaskEntry* entry, int selected) {
    char* cursor = line;

    cursor = appendText(cursor, selected ? "> " : "  ");
    cursor = appendUnsignedLong(cursor, (unsigned long)entry->pid);
    cursor = appendText(cursor, " ");
    cursor = appendText(cursor, taskmanStateName(entry->info.state));
    cursor = appendText(cursor, " pr=");
    cursor = appendUnsignedLong(cursor, (unsigned long)entry->info.priority);
    cursor = appendText(cursor, " ");
    cursor = appendText(cursor, entry->info.name);
    *cursor = '\0';
}

static void taskmanBuildPathLine(char* line, const char* prefix, const TaskmanPathEntry* entry, int selected) {
    char* cursor = line;

    cursor = appendText(cursor, selected ? "> " : "  ");
    cursor = appendText(cursor, prefix);
    cursor = appendText(cursor, entry->name);
    *cursor = '\0';
}

static void taskmanLoadDirectory(const char* path, const char* suffix, TaskmanPathEntry* entries, unsigned int capacity, unsigned int* outCount) {
    unsigned int count = 0U;

    if (!entries || !outCount) {
        return;
    }

    for (unsigned long index = 0UL; count < capacity; index++) {
        UserDirectoryEntry entry;
        long status = readDirectoryEntry(path, index, &entry);
        char fullPath[TASKMAN_NAME_MAX];
        char* cursor;

        if (status <= 0) {
            break;
        }
        if ((entry.attr & 0x10UL) != 0UL) {
            continue;
        }
        if (suffix && !taskmanTextEndsWith(entry.name, suffix)) {
            continue;
        }

        taskmanCopyText(entries[count].name, sizeof(entries[count].name), entry.name);
        cursor = fullPath;
        cursor = appendText(cursor, path);
        if (textLength(path) > 0UL && path[textLength(path) - 1UL] != '/') {
            *cursor++ = '/';
        }
        cursor = appendText(cursor, entry.name);
        *cursor = '\0';
        taskmanCopyText(entries[count].path, sizeof(entries[count].path), fullPath);
        count++;
    }

    *outCount = count;
}

static void taskmanRefreshTasks(TaskmanAppState* state) {
    unsigned int count = 0U;

    state->runningCount = 0U;
    state->readyCount = 0U;
    state->sleepingCount = 0U;
    state->blockedCount = 0U;

    for (long pid = 0; pid < 64 && count < TASKMAN_TASK_MAX; pid++) {
        UserTaskInfo info;

        if (getTaskInfo(pid, &info) <= 0) {
            continue;
        }

        state->tasks[count].pid = pid;
        state->tasks[count].info = info;
        if (info.state == 1) {
            state->readyCount++;
        }
        else if (info.state == 2) {
            state->runningCount++;
        }
        else if (info.state == 3) {
            state->sleepingCount++;
        }
        else if (info.state == 4) {
            state->blockedCount++;
        }
        count++;
    }

    state->taskCount = count;
    if (state->selectedTask >= state->taskCount) {
        state->selectedTask = state->taskCount > 0U ? (state->taskCount - 1U) : 0U;
    }
}

static void taskmanRefreshModel(TaskmanAppState* state) {
    taskmanRefreshTasks(state);
    taskmanLoadDirectory("/bin", ".exe", state->programs, TASKMAN_PROGRAM_MAX, &state->programCount);
    taskmanLoadDirectory("/lib", ".dll", state->libraries, TASKMAN_LIBRARY_MAX, &state->libraryCount);
    taskmanLoadDirectory("/system", ".sys", state->drivers, TASKMAN_DRIVER_MAX, &state->driverCount);
    (void)getMemoryInfo(&state->memInfo);
    if (state->selectedProgram >= state->programCount) {
        state->selectedProgram = state->programCount > 0U ? (state->programCount - 1U) : 0U;
    }
}

static TaskmanWindowState* taskmanWindow(TaskmanAppState* state, TaskmanWindowId id) {
    return &state->windows[id];
}

static void taskmanBringWindowToFront(TaskmanAppState* state, TaskmanWindowId id) {
    unsigned int index;

    for (index = 0U; index < TaskmanWindowCount; index++) {
        if (state->order[index] == id) {
            break;
        }
    }
    while (index + 1U < TaskmanWindowCount) {
        state->order[index] = state->order[index + 1U];
        index++;
    }
    state->order[TaskmanWindowCount - 1U] = id;
}

static void taskmanLaunchSelectedProgram(TaskmanAppState* state) {
    char name[64];

    if (state->selectedProgram >= state->programCount) {
        return;
    }

    taskmanBuildProgramName(state->programs[state->selectedProgram].path, name, sizeof(name));
    if (spawnTask(state->programs[state->selectedProgram].path, name, "") >= 0) {
        writeLine("taskman.exe: spawned selected program");
    }
}

static void taskmanKillSelectedTask(TaskmanAppState* state) {
    long pid;

    if (state->selectedTask >= state->taskCount) {
        return;
    }

    pid = state->tasks[state->selectedTask].pid;
    if (pid <= 0) {
        return;
    }

    (void)killTask(pid);
}

static void taskmanHandleTaskClick(TaskmanAppState* state, uint32_t x, uint32_t y) {
    TaskmanWindowState* window = taskmanWindow(state, TaskmanWindowTasks);
    WindowKitWindow spec;

    spec.visible = 1U;
    spec.x = window->x;
    spec.y = window->y;
    spec.width = window->width;
    spec.height = window->height;
    for (unsigned int index = 0U; index < state->taskCount && index < 10U; index++) {
        if (windowKitPointInLine(&spec, index, x, y)) {
            state->selectedTask = index;
            return;
        }
    }
}

static void taskmanHandleProgramClick(TaskmanAppState* state, uint32_t x, uint32_t y) {
    TaskmanWindowState* window = taskmanWindow(state, TaskmanWindowPrograms);
    WindowKitWindow spec;
    unsigned int displayCount = state->programCount < 8U ? state->programCount : 8U;

    spec.visible = 1U;
    spec.x = window->x;
    spec.y = window->y;
    spec.width = window->width;
    spec.height = window->height;

    for (unsigned int index = 0U; index < displayCount; index++) {
        if (windowKitPointInLine(&spec, index, x, y)) {
            state->selectedProgram = index;
            return;
        }
    }

    if (windowKitPointInLine(&spec, displayCount + 1U, x, y)) {
        taskmanLaunchSelectedProgram(state);
    }
}

static void taskmanHandleDetailsClick(TaskmanAppState* state, uint32_t x, uint32_t y) {
    TaskmanWindowState* window = taskmanWindow(state, TaskmanWindowDetails);
    WindowKitWindow spec;

    spec.visible = 1U;
    spec.x = window->x;
    spec.y = window->y;
    spec.width = window->width;
    spec.height = window->height;

    if (windowKitPointInLine(&spec, 8U, x, y)) {
        taskmanKillSelectedTask(state);
    }
}

static void taskmanHandlePointer(TaskmanAppState* state) {
    if (state->pointer.pressed && !state->lastPointer.pressed) {
        for (int orderIndex = (int)TaskmanWindowCount - 1; orderIndex >= 0; orderIndex--) {
            TaskmanWindowId id = state->order[orderIndex];
            TaskmanWindowState* window = taskmanWindow(state, id);
            WindowKitWindow spec;

            spec.visible = 1U;
            spec.x = window->x;
            spec.y = window->y;
            spec.width = window->width;
            spec.height = window->height;

            if (windowKitPointInTitleBar(&spec, state->pointer.x, state->pointer.y)) {
                taskmanBringWindowToFront(state, id);
                state->dragWindow = (int)id;
                state->dragOffsetX = (int)state->pointer.x - (int)window->x;
                state->dragOffsetY = (int)state->pointer.y - (int)window->y;
                return;
            }

            if (!windowKitPointInRect(state->pointer.x, state->pointer.y, window->x, window->y, window->width, window->height)) {
                continue;
            }

            taskmanBringWindowToFront(state, id);

            if (id == TaskmanWindowTasks) {
                taskmanHandleTaskClick(state, state->pointer.x, state->pointer.y);
            }
            else if (id == TaskmanWindowPrograms) {
                taskmanHandleProgramClick(state, state->pointer.x, state->pointer.y);
            }
            else if (id == TaskmanWindowDetails) {
                taskmanHandleDetailsClick(state, state->pointer.x, state->pointer.y);
            }
            return;
        }
    }

    if (state->pointer.pressed && state->dragWindow >= 0) {
        TaskmanWindowState* window = taskmanWindow(state, (TaskmanWindowId)state->dragWindow);

        window->x = (uint32_t)((int)state->pointer.x - state->dragOffsetX);
        window->y = (uint32_t)((int)state->pointer.y - state->dragOffsetY);
    }

    if (!state->pointer.pressed) {
        state->dragWindow = -1;
    }

    state->lastPointer = state->pointer;
}

static void taskmanAppendTasksWindow(TaskmanAppState* state, WindowKitFrame* frame) {
    TaskmanWindowState* window = taskmanWindow(state, TaskmanWindowTasks);
    WindowKitWindow* desktopWindow = windowKitAddWindow(frame, window->title, window->x, window->y, window->width, window->height, window->accentColor);
    char line[ROS_KERNEL_GUI_DESKTOP_TEXT_MAX];
    unsigned int displayCount = state->taskCount < 10U ? state->taskCount : 10U;

    if (!desktopWindow) {
        return;
    }

    for (unsigned int index = 0U; index < displayCount; index++) {
        taskmanBuildTaskLine(line, &state->tasks[index], index == state->selectedTask);
        windowKitAppendLine(desktopWindow, line);
    }
}

static void taskmanAppendDetailsWindow(TaskmanAppState* state, WindowKitFrame* frame) {
    TaskmanWindowState* window = taskmanWindow(state, TaskmanWindowDetails);
    WindowKitWindow* desktopWindow = windowKitAddWindow(frame, window->title, window->x, window->y, window->width, window->height, window->accentColor);
    char line[ROS_KERNEL_GUI_DESKTOP_TEXT_MAX];
    TaskmanTaskEntry* entry;

    if (!desktopWindow) {
        return;
    }
    if (state->taskCount == 0U || state->selectedTask >= state->taskCount) {
        windowKitAppendLine(desktopWindow, "No active tasks");
        return;
    }

    entry = &state->tasks[state->selectedTask];
    taskmanBuildUnsignedLine(line, "pid=", (unsigned long)entry->pid);
    windowKitAppendLine(desktopWindow, line);
    taskmanBuildUnsignedLine(line, "thread=", (unsigned long)entry->info.thread_id);
    windowKitAppendLine(desktopWindow, line);
    taskmanBuildUnsignedLine(line, "parent=", (unsigned long)entry->info.parent_process_id);
    windowKitAppendLine(desktopWindow, line);
    taskmanCopyText(line, sizeof(line), taskmanStateName(entry->info.state));
    windowKitAppendLine(desktopWindow, line);

    {
        char* cursor = line;
        cursor = appendText(cursor, "prio=");
        cursor = appendUnsignedLong(cursor, (unsigned long)entry->info.priority);
        cursor = appendText(cursor, " counter=");
        cursor = appendUnsignedLong(cursor, (unsigned long)entry->info.counter);
        *cursor = '\0';
    }
    windowKitAppendLine(desktopWindow, line);

    {
        char* cursor = line;
        cursor = appendText(cursor, "flags=0x");
        cursor = appendHex(cursor, entry->info.flags);
        *cursor = '\0';
    }
    windowKitAppendLine(desktopWindow, line);

    {
        char* cursor = line;
        cursor = appendText(cursor, "cpu run=");
        cursor = appendUnsignedLong(cursor, state->runningCount);
        cursor = appendText(cursor, " ready=");
        cursor = appendUnsignedLong(cursor, state->readyCount);
        *cursor = '\0';
    }
    windowKitAppendLine(desktopWindow, line);

    {
        char* cursor = line;
        cursor = appendText(cursor, "sleep=");
        cursor = appendUnsignedLong(cursor, state->sleepingCount);
        cursor = appendText(cursor, " block=");
        cursor = appendUnsignedLong(cursor, state->blockedCount);
        *cursor = '\0';
    }
    windowKitAppendLine(desktopWindow, line);
    windowKitAppendLine(desktopWindow, "[End Selected Task]");
}

static void taskmanAppendProgramsWindow(TaskmanAppState* state, WindowKitFrame* frame) {
    TaskmanWindowState* window = taskmanWindow(state, TaskmanWindowPrograms);
    WindowKitWindow* desktopWindow = windowKitAddWindow(frame, window->title, window->x, window->y, window->width, window->height, window->accentColor);
    char line[ROS_KERNEL_GUI_DESKTOP_TEXT_MAX];
    unsigned int displayCount = state->programCount < 8U ? state->programCount : 8U;

    if (!desktopWindow) {
        return;
    }

    for (unsigned int index = 0U; index < displayCount; index++) {
        taskmanBuildPathLine(line, "", &state->programs[index], index == state->selectedProgram);
        windowKitAppendLine(desktopWindow, line);
    }
    windowKitAppendLine(desktopWindow, " ");
    windowKitAppendLine(desktopWindow, "[Run Selected Program]");
}

static void taskmanAppendStorageWindow(TaskmanAppState* state, WindowKitFrame* frame) {
    TaskmanWindowState* window = taskmanWindow(state, TaskmanWindowStorage);
    WindowKitWindow* desktopWindow = windowKitAddWindow(frame, window->title, window->x, window->y, window->width, window->height, window->accentColor);
    char line[ROS_KERNEL_GUI_DESKTOP_TEXT_MAX];

    if (!desktopWindow) {
        return;
    }

    taskmanBuildUnsignedLine(line, "mem total=", state->memInfo.total_bytes);
    windowKitAppendLine(desktopWindow, line);
    taskmanBuildUnsignedLine(line, "mem free=", state->memInfo.free_bytes);
    windowKitAppendLine(desktopWindow, line);
    taskmanBuildUnsignedLine(line, "page size=", state->memInfo.page_size);
    windowKitAppendLine(desktopWindow, line);
    taskmanBuildUnsignedLine(line, "free pages=", state->memInfo.free_pages);
    windowKitAppendLine(desktopWindow, line);
    windowKitAppendLine(desktopWindow, "libs:");
    for (unsigned int index = 0U; index < state->libraryCount && index < 3U; index++) {
        taskmanBuildPathLine(line, "  ", &state->libraries[index], 0);
        windowKitAppendLine(desktopWindow, line);
    }
    windowKitAppendLine(desktopWindow, "drivers:");
    for (unsigned int index = 0U; index < state->driverCount && index < 2U; index++) {
        taskmanBuildPathLine(line, "  ", &state->drivers[index], 0);
        windowKitAppendLine(desktopWindow, line);
    }
}

static void taskmanBuildFrame(TaskmanAppState* state, WindowKitFrame* frame) {
    windowKitResetFrame(frame, TASKMAN_BACKGROUND);
    for (unsigned int orderIndex = 0U; orderIndex < TaskmanWindowCount; orderIndex++) {
        TaskmanWindowId id = state->order[orderIndex];

        if (id == TaskmanWindowTasks) {
            taskmanAppendTasksWindow(state, frame);
        }
        else if (id == TaskmanWindowDetails) {
            taskmanAppendDetailsWindow(state, frame);
        }
        else if (id == TaskmanWindowPrograms) {
            taskmanAppendProgramsWindow(state, frame);
        }
        else if (id == TaskmanWindowStorage) {
            taskmanAppendStorageWindow(state, frame);
        }
    }
}

static void taskmanInit(TaskmanAppState* state) {
    for (unsigned int index = 0U; index < TaskmanWindowCount; index++) {
        state->order[index] = (TaskmanWindowId)index;
    }

    state->windows[TaskmanWindowTasks].x = 36U;
    state->windows[TaskmanWindowTasks].y = 68U;
    state->windows[TaskmanWindowTasks].width = 300U;
    state->windows[TaskmanWindowTasks].height = 250U;
    state->windows[TaskmanWindowTasks].accentColor = 0x001D4ED8U;
    state->windows[TaskmanWindowTasks].title = "Tasks";

    state->windows[TaskmanWindowDetails].x = 356U;
    state->windows[TaskmanWindowDetails].y = 68U;
    state->windows[TaskmanWindowDetails].width = 300U;
    state->windows[TaskmanWindowDetails].height = 250U;
    state->windows[TaskmanWindowDetails].accentColor = 0x000F766EU;
    state->windows[TaskmanWindowDetails].title = "Task Struct";

    state->windows[TaskmanWindowPrograms].x = 676U;
    state->windows[TaskmanWindowPrograms].y = 68U;
    state->windows[TaskmanWindowPrograms].width = 300U;
    state->windows[TaskmanWindowPrograms].height = 250U;
    state->windows[TaskmanWindowPrograms].accentColor = 0x00B45309U;
    state->windows[TaskmanWindowPrograms].title = "Run New Task";

    state->windows[TaskmanWindowStorage].x = 120U;
    state->windows[TaskmanWindowStorage].y = 344U;
    state->windows[TaskmanWindowStorage].width = 420U;
    state->windows[TaskmanWindowStorage].height = 220U;
    state->windows[TaskmanWindowStorage].accentColor = 0x007C3AEDU;
    state->windows[TaskmanWindowStorage].title = "Memory Libraries Drivers";

    state->dragWindow = -1;
}

int AppMain(void) {
    TaskmanAppState* state = &g_taskmanState;
    WindowKitFrame* frame = &g_taskmanFrame;
    unsigned char* bytes = (unsigned char*)state;

    for (unsigned long index = 0UL; index < sizeof(*state); index++) {
        bytes[index] = 0U;
    }

    writeLine("taskman.exe: starting desktop task manager");
    taskmanInit(state);
    (void)windowKitShowDesktop(1UL);

    for (;;) {
        taskmanRefreshModel(state);
        if (windowKitQueryPointer(&state->pointer) == 0) {
            taskmanHandlePointer(state);
        }
        taskmanBuildFrame(state, frame);
        (void)windowKitPresent(frame);
        (void)sleepMs(TASKMAN_REFRESH_MSEC);
    }
}