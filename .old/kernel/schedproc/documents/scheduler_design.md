# my-schedproc Scheduler Design

This local module document is the implementation-facing companion to the workspace-level scheduler study.

The first milestone implemented in this scaffold is:
- single-CPU scheduler context
- per-priority ready queues
- ordered sleep queue
- equal-priority round-robin
- explicit block, unblock, sleep, and dispatch APIs

The next milestones are:
- wait-node proxy layer
- mutex ownership tracking and priority inheritance
- process-driven thread teardown
- per-CPU queues and affinity routing

Primary public header:
- `include/sp_scheduler.h`

Primary implementation file:
- `src/sp_scheduler.c`
