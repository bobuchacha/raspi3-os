#ifndef APPLICATIONS_ROS_USER_RUNTIME_H
#define APPLICATIONS_ROS_USER_RUNTIME_H

/*
 * Compatibility shim for the older SDK header name.
 *
 * New user-space code should include user_runtime.h directly and use the
 * de-prefixed API names.
 */

#include "user_runtime.h"

#define ROS_LDR_META_VERSION USER_LDR_META_VERSION
#define ROS_LDR_META_KIND_EXPORT USER_LDR_META_KIND_EXPORT
#define ROS_LDR_META_NAME_MAX USER_LDR_META_NAME_MAX

#define ROS_SYS_WRITE USER_SYS_WRITE
#define ROS_SYS_EXIT USER_SYS_EXIT
#define ROS_SYS_SLEEP USER_SYS_SLEEP
#define ROS_SYS_SHLIB_OPEN USER_SYS_SHLIB_OPEN
#define ROS_SYS_TASK_NAME USER_SYS_TASK_NAME
#define ROS_SYS_SPAWN USER_SYS_SPAWN
#define ROS_SYS_CONSOLE_READ USER_SYS_CONSOLE_READ
#define ROS_SYS_READ_FILE USER_SYS_READ_FILE
#define ROS_SYS_DIR_ENTRY USER_SYS_DIR_ENTRY
#define ROS_SYS_MKDIR USER_SYS_MKDIR
#define ROS_SYS_TASK_INFO USER_SYS_TASK_INFO
#define ROS_SYS_MEM_INFO USER_SYS_MEM_INFO
#define ROS_SYS_KILL USER_SYS_KILL
#define ROS_SYS_REBOOT USER_SYS_REBOOT
#define ROS_SYS_DEBUG_SHELL USER_SYS_DEBUG_SHELL
#define ROS_SYS_TASK_ARGS USER_SYS_TASK_ARGS
#define ROS_SYS_SHLIB_EXPORT USER_SYS_SHLIB_EXPORT
#define ROS_SYS_SHLIB_CLOSE USER_SYS_SHLIB_CLOSE
#define ROS_SYS_DRIVER_UNLOAD USER_SYS_DRIVER_UNLOAD
#define ROS_SYS_WAIT_PID USER_SYS_WAIT_PID
#define ROS_SYS_GUI_CONTROL USER_SYS_GUI_CONTROL
#define ROS_SYS_GUI_SET_TASKBAR_TEXT USER_SYS_GUI_SET_TASKBAR_TEXT
#define ROS_SYS_UPTIME_MSEC USER_SYS_UPTIME_MSEC
#define ROS_SYS_LOG_SEND USER_SYS_LOG_SEND
#define ROS_SYS_LOG_RECV USER_SYS_LOG_RECV
#define ROS_SYS_LOG_WRITE USER_SYS_LOG_WRITE
#define ROS_USER_LOADER_ACTION_BIT USER_LOADER_ACTION_BIT

typedef UserDirectoryEntry RosUserDirectoryEntry;
typedef UserTaskInfo RosUserTaskInfo;
typedef UserMemInfo RosUserMemInfo;
typedef UserLoaderExportMeta RosLdrExportMeta;

#define ROS_LDR_EMIT_EXPORT LDR_EMIT_EXPORT
#define ROS_DLL_EXPORT DLL_EXPORT
#define ROS_SYS_EXPORT SYS_EXPORT
#define ROS_DLL_EXPORT_AS DLL_EXPORT_AS
#define ROS_SYS_EXPORT_AS SYS_EXPORT_AS
#define ROS_DLL_CACHE DLL_CACHE
#define ROS_DLL_RESOLVE DLL_RESOLVE

#define ros_shlib_resolve_cached resolveSharedLibraryCached
#define ros_syscall0 invokeSyscall0
#define ros_syscall1 invokeSyscall1
#define ros_syscall2 invokeSyscall2
#define ros_syscall3 invokeSyscall3
#define ros_syscall4 invokeSyscall4
#define ros_strlen textLength
#define ros_append_text appendText
#define ros_append_ulong appendUnsignedLong
#define ros_append_hex appendHex
#define ros_write writeText
#define ros_write_line writeLine
#define ros_exit exitProcess
#define ros_sleep sleepMs
#define ros_task_name getTaskName
#define ros_task_args getTaskArgs
#define ros_console_read readConsole
#define ros_read_file readFile
#define ros_dir_entry readDirectoryEntry
#define ros_mkdir makeDirectory
#define ros_task_info getTaskInfo
#define ros_wait_pid waitPid
#define ros_gui_control controlGui
#define ros_gui_set_taskbar_text setGuiTaskbarText
#define ros_uptime_msec getUptimeMs
#define ros_log_send sendLog
#define ros_log_recv receiveLog
#define ros_log_write writeLog
#define ros_mem_info getMemoryInfo
#define ros_kill killTask
#define ros_reboot rebootSystem
#define ros_debug_shell runDebugShell
#define ros_shlib_open openSharedLibrary
#define ros_spawn spawnTask
#define ros_shlib_export exportSharedLibrary
#define ros_shlib_close closeSharedLibrary
#define ros_driver_unload unloadDriver

#endif
