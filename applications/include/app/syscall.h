#ifndef ROS_APP_SYSCALL_H
#define ROS_APP_SYSCALL_H

#include "user_runtime.h"
#include "user_ipc.h"

#define SYS_NUM_OF_ENTRIES 55

#define SYS_WRITE_NUMBER 0
#define SYS_MALLOC_NUMBER 1
#define SYS_FREE_NUMBER 2
#define SYS_FORK_NUMBER 3
#define SYS_EXIT_NUMBER 4
#define SYS_GET_PARAM 5
#define SYS_SLEEP_NUMBER 6
#define SYS_EXEC_NUMBER 7
#define SYS_SHLIB_OPEN_NUMBER 8
#define SYS_TASK_NAME_NUMBER 9
#define SYS_SPAWN_NUMBER 10
#define SYS_SHLIB_LOCAL_NUMBER 11
#define SYS_CONSOLE_READ_NUMBER 12
#define SYS_READ_FILE_NUMBER 13
#define SYS_DIR_ENTRY_NUMBER 14
#define SYS_MKDIR_NUMBER 15
#define SYS_TASK_INFO_NUMBER 16
#define SYS_MEM_INFO_NUMBER 17
#define SYS_KILL_NUMBER 18
#define SYS_REBOOT_NUMBER 19
#define SYS_DEBUG_SHELL_NUMBER 20
#define SYS_EXT_INVOKE_NUMBER 21
#define SYS_TASK_ARGS_NUMBER 22
#define SYS_MODULE_INVOKE_NUMBER 23
#define SYS_SHLIB_EXPORT_NUMBER 24
#define SYS_SHLIB_CLOSE_NUMBER 25
#define SYS_DRIVER_UNLOAD_NUMBER 26
#define SYS_WAIT_PID_NUMBER 27
#define SYS_GUI_CONTROL_NUMBER 28
#define SYS_GUI_SET_TASKBAR_TEXT_NUMBER 29
#define SYS_UPTIME_MSEC_NUMBER 30
#define SYS_LOG_SEND_NUMBER 31
#define SYS_LOG_RECV_NUMBER 32
#define SYS_LOG_WRITE_NUMBER 33
#define SYS_EVENT_SUBSCRIBE_NUMBER 34
#define SYS_EVENT_READ_NUMBER 35
#define SYS_EVENT_QUERY_NUMBER 36
#define SYS_EVENT_UNSUBSCRIBE_NUMBER 37
#define SYS_EVENT_WAIT_NUMBER 38
#define SYS_REMOVE_NUMBER 39
#define SYS_IPC_SEND_NUMBER 40
#define SYS_IPC_RECV_NUMBER 41
#define SYS_CREATE_THREAD_NUMBER 42
#define SYS_SET_THREAD_PRIORITY_NUMBER 43
#define SYS_SPAWN_ASYNC_NUMBER 44
#define SYS_SPAWN_ASYNC_RESULT_NUMBER 45
#define SYS_FILE_MAPPING_CREATE_NUMBER 46
#define SYS_FILE_MAPPING_OPEN_NUMBER 47
#define SYS_FILE_MAPPING_CLOSE_NUMBER 48
#define SYS_FILE_MAPPING_MAP_NUMBER 49
#define SYS_FILE_MAPPING_UNMAP_NUMBER 50
#define SYS_TASK_MODULES_NUMBER 51
#define SYS_PATH_INFO_NUMBER 53
#define SYS_EXIT_THREAD_NUMBER 54

#define SYS_WRITE 0
#define SYS_MALLOC 1
#define SYS_FREE 2
#define SYS_FORK 3
#define SYS_EXIT 4
#define SYS_GET_PARAM_NUMBER 5
#define SYS_SLEEP 6
#define SYS_EXEC 7
#define SYS_SHLIB_OPEN 8
#define SYS_TASK_NAME 9
#define SYS_SPAWN 10
#define SYS_SHLIB_LOCAL 11
#define SYS_CONSOLE_READ 12
#define SYS_READ_FILE 13
#define SYS_DIR_ENTRY 14
#define SYS_MKDIR 15
#define SYS_TASK_INFO 16
#define SYS_MEM_INFO 17
#define SYS_KILL 18
#define SYS_REBOOT 19
#define SYS_DEBUG_SHELL 20
#define SYS_EXT_INVOKE 21
#define SYS_TASK_ARGS 22
#define SYS_MODULE_INVOKE 23
#define SYS_SHLIB_EXPORT 24
#define SYS_SHLIB_CLOSE 25
#define SYS_DRIVER_UNLOAD 26
#define SYS_WAIT_PID 27
#define SYS_GUI_CONTROL 28
#define SYS_GUI_SET_TASKBAR_TEXT 29
#define SYS_UPTIME_MSEC 30
#define SYS_LOG_SEND 31
#define SYS_LOG_RECV 32
#define SYS_LOG_WRITE 33
#define SYS_EVENT_SUBSCRIBE 34
#define SYS_EVENT_READ 35
#define SYS_EVENT_QUERY 36
#define SYS_EVENT_UNSUBSCRIBE 37
#define SYS_EVENT_WAIT 38
#define SYS_REMOVE 39
#define SYS_IPC_SEND 40
#define SYS_IPC_RECV 41
#define SYS_CREATE_THREAD 42
#define SYS_SET_THREAD_PRIORITY 43
#define SYS_SPAWN_ASYNC 44
#define SYS_SPAWN_ASYNC_RESULT 45
#define SYS_FILE_MAPPING_CREATE 46
#define SYS_FILE_MAPPING_OPEN 47
#define SYS_FILE_MAPPING_CLOSE 48
#define SYS_FILE_MAPPING_MAP 49
#define SYS_FILE_MAPPING_UNMAP 50
#define SYS_TASK_MODULES 51
#define SYS_PATH_INFO 53
#define SYS_EXIT_THREAD 54

#ifndef __ASSEMBLER__
#ifdef __cplusplus
extern "C"
{
#endif

#ifndef APPLICATIONS_USER_DIRECTORY_ENTRY_DEFINED
#define APPLICATIONS_USER_DIRECTORY_ENTRY_DEFINED
    typedef struct UserDirectoryEntry {
        char name[128];
        unsigned long size;
        unsigned long attr;
    } UserDirectoryEntry;
#endif

#ifndef APPLICATIONS_USER_PATH_INFO_DEFINED
#define APPLICATIONS_USER_PATH_INFO_DEFINED
    typedef struct UserPathInfo {
        unsigned long size;
        unsigned long type;
        unsigned long backend_kind;
        unsigned long flags;
        unsigned long volume_letter;
        char device_name[16];
    } UserPathInfo;
#endif

#ifndef APPLICATIONS_USER_TASK_INFO_DEFINED
#define APPLICATIONS_USER_TASK_INFO_DEFINED
#ifndef APPLICATIONS_USER_TASK_STATE_CODES_DEFINED
#define APPLICATIONS_USER_TASK_STATE_CODES_DEFINED
#define USER_TASK_STATE_INITIALIZED 0UL
#define USER_TASK_STATE_READY 1UL
#define USER_TASK_STATE_RUNNING 2UL
#define USER_TASK_STATE_WAITING 3UL
#define USER_TASK_STATE_SUSPENDED 4UL
#define USER_TASK_STATE_TERMINATED 5UL
#endif

#ifndef APPLICATIONS_USER_TASK_WAIT_REASON_CODES_DEFINED
#define APPLICATIONS_USER_TASK_WAIT_REASON_CODES_DEFINED
#define USER_TASK_WAIT_NONE 0UL
#define USER_TASK_WAIT_DELAY 1UL
#define USER_TASK_WAIT_EVENT 2UL
#define USER_TASK_WAIT_MUTEX 3UL
#define USER_TASK_WAIT_SEMAPHORE 4UL
#define USER_TASK_WAIT_MESSAGE 5UL
#define USER_TASK_WAIT_IO 6UL
#endif

    typedef struct UserTaskInfo {
        long id;
        long thread_id;
        long parent_process_id;
        long main_thread_state;
        long wait_reason;
        long exit_code;
        long scheduler_ticks;
        long current_priority;
        unsigned long flags;
        char name[32];
    } UserTaskInfo;
#endif

#ifndef APPLICATIONS_USER_MEM_INFO_DEFINED
#define APPLICATIONS_USER_MEM_INFO_DEFINED
    typedef struct UserMemInfo {
        unsigned long total_bytes;
        unsigned long free_bytes;
        unsigned long page_size;
        unsigned long free_pages;
    } UserMemInfo;
#endif

#ifndef APPLICATIONS_USER_FILE_MAPPING_HANDLE_DEFINED
#define APPLICATIONS_USER_FILE_MAPPING_HANDLE_DEFINED
    typedef unsigned long FileMappingHandle;
#endif

    long syscall(long number, ...);
    long call_sys_write(char* buf);
    long call_sys_exit(unsigned long result);
    long call_sys_fork(void);
    int call_sys_get_param(int param);
    long call_sys_sleep(unsigned long msec);
    long call_sys_exec(const char* path);
    unsigned long call_sys_shlib_open(const char* path);
    unsigned long call_sys_shlib_local(const char* path, unsigned long size);
    long call_sys_task_name(char* buf, unsigned long size);
    long call_sys_spawn(const char* path, const char* name, const char* args);
    long call_sys_task_args(char* buf, unsigned long size);
    long call_sys_console_read(void);
    long call_sys_read_file(const char* path, unsigned long offset, char* buf, unsigned long size);
    long call_sys_dir_entry(const char* path, unsigned long entry_index, UserDirectoryEntry* entry);
    long call_sys_path_info(const char* path, UserPathInfo* info);
    long call_sys_mkdir(const char* path);
    long call_sys_remove(const char* path);
    long call_sys_task_info(long pid, UserTaskInfo* info);
    long call_sys_wait_pid(long pid, long* result);
    long call_sys_mem_info(UserMemInfo* info);
    long call_sys_kill(long pid);
    long call_sys_reboot(void);
    long call_sys_debug_shell(const char* line);
    long call_sys_module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result);
    unsigned long call_sys_shlib_export(const char* path, const char* export_name);
    long call_sys_shlib_close(const char* path);
    long call_sys_driver_unload(const char* path);
    long call_sys_ext_invoke(const char* ext, const char* func, unsigned long a, unsigned long b);
    long call_sys_gui_control(unsigned long command, unsigned long value);
    long call_sys_gui_set_taskbar_text(const char* clock_text, const char* date_text);
    unsigned long call_sys_uptime_msec(void);
    long call_sys_log_send(const char* text);
    long call_sys_log_recv(char* buffer, unsigned long size);
    long call_sys_log_write(const char* text);
    long call_sys_event_wait(unsigned long subscription_id);
    long call_sys_ipc_send(long receiver_pid, const UserIpcMessage* message);
    long call_sys_ipc_recv(UserIpcMessage* message, unsigned long flags);
    long call_sys_create_thread(unsigned long entry_point, unsigned long argument, const char* name);
    long call_sys_exit_thread(void);
    long call_sys_spawn_async(const char* path, const char* name, const char* args);
    long call_sys_spawn_async_result(UserAsyncSpawnResult* result);
    unsigned long call_sys_file_mapping_create(const char* path, unsigned long size);
    unsigned long call_sys_file_mapping_open(const char* path, unsigned long size);
    long call_sys_file_mapping_close(unsigned long handle);
    unsigned long call_sys_file_mapping_map(unsigned long handle, unsigned long offset, unsigned long size);
    long call_sys_file_mapping_unmap(unsigned long address);
    void user_delay(unsigned long msec);
    void user_spin_delay(unsigned long count);

#ifdef __cplusplus
}
#endif
#endif

#endif
