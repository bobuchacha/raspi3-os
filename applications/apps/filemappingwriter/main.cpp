#define ROS_APP_WITH_CRT 1

#include "app/app.h"
#include "filemapping_demo.h"

static unsigned long filemapping_writer_text_length(const char* text) {
    unsigned long length = 0UL;

    if (text == 0) {
        return 0UL;
    }

    while (text[length] != '\0') {
        ++length;
    }

    return length;
}

static void filemapping_writer_copy_text(char* destination, unsigned long capacity, const char* source, unsigned long length) {
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

static long filemapping_writer_open_mapping(FileMappingHandle* handleOut) {
    return openFileMapping(FILEMAPPING_DEMO_PATH, filemapping_demo_state_size(), handleOut);
}

int main(void) {
    char task_name[64] = { 0 };
    char task_args[128] = { 0 };
    FileMappingHandle handle = 0UL;
    void* mapped_view = 0;
    FileMappingDemoState* state;
    unsigned long input_length;
    unsigned long next_counter;
    long status;

    if (getTaskName(task_name, sizeof(task_name)) < 0) {
        task_name[0] = '\0';
    }
    if (getTaskArgs(task_args, sizeof(task_args)) < 0) {
        task_args[0] = '\0';
    }

    crt_printf(
        "filemapping_writer.exe: task=%s args=%s\r\n",
        task_name[0] != '\0' ? task_name : "<unknown>",
        task_args[0] != '\0' ? task_args : "<none>");

    if (task_args[0] == '\0') {
        crt_printf("filemapping_writer.exe: pass a message argument\r\n");
        return 1;
    }

    status = filemapping_writer_open_mapping(&handle);
    if (status < 0) {
        crt_printf("filemapping_writer.exe: mapping open failed status=%ld\r\n", status);
        return 2;
    }

    status = mapFileMappingView(handle, 0UL, filemapping_demo_state_size(), &mapped_view);
    if ((status < 0) || (mapped_view == 0)) {
        crt_printf("filemapping_writer.exe: map failed status=%ld\r\n", status);
        (void)closeFileMapping(handle);
        return 3;
    }

    state = (FileMappingDemoState*)mapped_view;
    input_length = filemapping_writer_text_length(task_args);
    if (input_length >= sizeof(state->text)) {
        input_length = sizeof(state->text) - 1UL;
    }

    filemapping_writer_copy_text(state->text, sizeof(state->text), task_args, input_length);
    state->text_length = input_length;
    next_counter = state->counter + 1UL;
    state->counter = next_counter;

    crt_printf(
        "filemapping_writer.exe: wrote counter=%lu text=\"%s\"\r\n",
        next_counter,
        state->text);

    (void)unmapFileMappingView(mapped_view);
    (void)closeFileMapping(handle);
    return 0;
}