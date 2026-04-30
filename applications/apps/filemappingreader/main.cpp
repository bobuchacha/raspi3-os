#define ROS_APP_WITH_CRT 1

#include "app/app.h"
#include "filemapping_demo.h"

#define FILEMAPPING_READER_POLL_MS 250UL

static void filemapping_reader_clear_state(FileMappingDemoState* state) {
    if (state == 0) {
        return;
    }

    state->counter = 0UL;
    state->text_length = 0UL;
    state->text[0] = '\0';
}

static void filemapping_reader_copy_text(char* destination, unsigned long capacity, const volatile char* source, unsigned long length) {
    unsigned long index = 0UL;

    if ((destination == 0) || (source == 0) || (capacity == 0UL)) {
        return;
    }

    if (length >= capacity) {
        length = capacity - 1UL;
    }

    while (index < length) {
        destination[index] = source[index];
        ++index;
    }

    destination[index] = '\0';
}

static int filemapping_reader_snapshot(
    volatile const FileMappingDemoState* state,
    unsigned long* counterOut,
    unsigned long* textLengthOut,
    char* textOut,
    unsigned long textCapacity) {

    unsigned long counterBefore;
    unsigned long counterAfter;
    unsigned long textLength;

    if ((state == 0) || (counterOut == 0) || (textLengthOut == 0) || (textOut == 0) || (textCapacity == 0UL)) {
        return -1;
    }

    counterBefore = state->counter;
    textLength = state->text_length;
    filemapping_reader_copy_text(textOut, textCapacity, state->text, textLength);
    counterAfter = state->counter;

    if (counterBefore != counterAfter) {
        return 1;
    }

    *counterOut = counterAfter;
    *textLengthOut = textLength;
    return 0;
}

static long filemapping_reader_open_mapping(FileMappingHandle* handleOut, int* createdNewOut) {
    long status;

    status = createFileMapping(FILEMAPPING_DEMO_PATH, filemapping_demo_state_size(), handleOut);
    if (status == StatusOK) {
        if (createdNewOut != 0) {
            *createdNewOut = 1;
        }
        return status;
    }

    if (status == StatusAlreadyExists) {
        if (createdNewOut != 0) {
            *createdNewOut = 0;
        }
        status = openFileMapping(FILEMAPPING_DEMO_PATH, filemapping_demo_state_size(), handleOut);
        return status;
    }

    return status;
}

int main(void) {
    char task_name[64] = { 0 };
    char task_args[128] = { 0 };
    FileMappingHandle handle = 0UL;
    void* mapped_view = 0;
    volatile FileMappingDemoState* state;
    FileMappingDemoState* mutable_state;
    unsigned long last_counter = 0UL;
    char text[FILEMAPPING_DEMO_TEXT_BYTES + 1U];
    int created_new = 0;
    long status;

    if (getTaskName(task_name, sizeof(task_name)) < 0) {
        task_name[0] = '\0';
    }
    if (getTaskArgs(task_args, sizeof(task_args)) < 0) {
        task_args[0] = '\0';
    }

    crt_printf(
        "filemapping_reader.exe: task=%s args=%s\r\n",
        task_name[0] != '\0' ? task_name : "<unknown>",
        task_args[0] != '\0' ? task_args : "<none>");

    status = filemapping_reader_open_mapping(&handle, &created_new);
    if (status < 0) {
        crt_printf("filemapping_reader.exe: mapping open failed status=%ld\r\n", status);
        return 1;
    }

    status = mapFileMappingView(handle, 0UL, filemapping_demo_state_size(), &mapped_view);
    if ((status < 0) || (mapped_view == 0)) {
        crt_printf("filemapping_reader.exe: map failed status=%ld\r\n", status);
        (void)closeFileMapping(handle);
        return 2;
    }

    state = (volatile FileMappingDemoState*)mapped_view;
    mutable_state = (FileMappingDemoState*)mapped_view;
    if (created_new != 0) {
        filemapping_reader_clear_state(mutable_state);
    }

    for (;;) {
        unsigned long initial_counter = 0UL;
        unsigned long initial_length = 0UL;

        status = filemapping_reader_snapshot(state, &initial_counter, &initial_length, text, sizeof(text));
        if (status == 0) {
            last_counter = initial_counter;
            (void)initial_length;
            break;
        }
    }

    crt_printf(
        "filemapping_reader.exe: ready counter=%lu text=\"%s\"\r\n",
        last_counter,
        text);

    for (;;) {
        unsigned long counter;
        unsigned long current_length;
        int snapshot_status;

        snapshot_status = filemapping_reader_snapshot(state, &counter, &current_length, text, sizeof(text));
        if (snapshot_status < 0) {
            crt_printf("filemapping_reader.exe: snapshot failed\r\n");
            break;
        }
        if (snapshot_status == 1) {
            continue;
        }

        if (counter != last_counter) {
            crt_printf(
                "filemapping_reader.exe: counter=%lu text=\"%s\"\r\n",
                counter,
                text);
            last_counter = counter;
        }

        (void)current_length;
        sleepMs(FILEMAPPING_READER_POLL_MS);
    }

    (void)unmapFileMappingView(mapped_view);
    (void)closeFileMapping(handle);
    return 0;
}