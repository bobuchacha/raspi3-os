#define SYS_NUM_OF_ENTRIES 25

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

#ifndef __ASSEMBLER__
#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct UserDirectoryEntry {
        char name[128];
        unsigned long size;
        unsigned long attr;
    } UserDirectoryEntry;

    typedef struct UserTaskInfo {
        long id;
        long state;
        long counter;
        long priority;
        unsigned long flags;
        char name[32];
    } UserTaskInfo;

    typedef struct UserMemInfo {
        unsigned long total_bytes;
        unsigned long free_bytes;
        unsigned long page_size;
        unsigned long free_pages;
    } UserMemInfo;

    long syscall(long number, ...);
    void call_sys_write(char* buf);
    unsigned long call_sys_malloc(unsigned long value);
    void call_sys_free(void* ptr);
    void call_sys_exit(unsigned long result);
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
    long call_sys_mkdir(const char* path);
    long call_sys_task_info(long pid, UserTaskInfo* info);
    long call_sys_mem_info(UserMemInfo* info);
    long call_sys_kill(long pid);
    void call_sys_reboot(void);
    long call_sys_debug_shell(const char* line);
    long call_sys_module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result);
    unsigned long call_sys_shlib_export(const char* path, const char* export_name);
    void user_delay(unsigned long msec);
    void user_spin_delay(unsigned long count);

#ifdef __cplusplus
}
#endif
#endif