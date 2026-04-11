# my-schedproc Process Manager Design

This local module document is the implementation-facing companion to the workspace-level process lifecycle study.

The first milestone implemented in this scaffold is:
- scheduler/process-manager shared context
- process creation in STARTING state
- thread creation in SUSPENDED state
- main-thread promotion of process to NORMAL state
- process exit transition to EXITING

The next milestones are:
- kernel/user stack allocation callbacks
- architecture context setup
- thread exit and final removal handshake with scheduler
- loader-driven process bootstrap hooks

Primary public headers:
- `include/sp_process.h`
- `include/sp_thread.h`

Primary implementation file:
- `src/sp_process.c`
