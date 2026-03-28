# User Process Callback Bridge (my-loader)

## Goal

Enable `ldr_load_exe` and `ldr_load_dll` to load into a target user task address space by bridging my-loader callbacks to existing `user-exe.c` task/MM helpers.

## Current state

- `ldr_kernel.c` currently provides kernel-only callbacks.
- `vm_reserve` uses `kmalloc`, which is correct for kernel modules but not for user task mappings.
- `proc_*` callbacks are stubs that return failure.
- `user-exe.c` already has a working LRD0 EXE/DLL loader path (`exec_load_lrd_exe`, `shared_library_load_lrd0`).

## Bridge design

### 1) Split contexts by domain

- Keep one my-loader context for kernel drivers.
- Add a second context type for user task loads.
- Do not mix kernel and user VM policies in one callback table.

### 2) User load session object

Define one per-load session that owns:

- target `Task*`
- optional staged task pointer for exec replacement
- allocation/page tracking for rollback
- resolved entry point and stack top outputs

This session is referenced from callback `opaque` state.

### 3) User VM callback contract

Implement user callbacks over existing task memory APIs:

- `vm_reserve(preferred,size,flags,out_base)`
- `vm_commit(base,size,flags)`
- `vm_protect(base,size,flags)`
- `vm_release(base,size)`

Expected behavior:

- reserve picks a VA range in user space (page aligned, conflict checked)
- commit allocates/maps pages into target task page tables
- protect updates PTE flags per section permissions
- release unmaps and frees session-owned pages

### 4) Process callbacks

Provide real user callbacks:

- `proc_add_module` records mapped module metadata in task/process module list
- `proc_set_entry` stores EL0 entry and stack for final commit
- `proc_start` remains optional for `exec` flow (already committed by scheduler path)

### 5) File/heap callbacks

- Reuse existing VFS callbacks.
- Heap callbacks continue to use kernel allocator for loader metadata only.

### 6) Import/DLL policy

- Keep import resolution behavior compatible with `load_user_shared_library_export` semantics.
- For first iteration, map imports by absolute module path or `/lib/<name>` fallback.

### 7) Integration sequence for `exec_user_program`

1. Detect LRD0 in `user-exe.c`.
2. Initialize a user-load session from staged task.
3. Call `ldr_load_exe` with user context and request path.
4. On success, commit `pc/sp` from bridge outputs and continue existing exec commit flow.
5. On failure, rollback through session release and keep old image.

### 8) Compatibility strategy

- Keep current manual LRD0 path behind a temporary fallback switch.
- Move one piece at a time:
  - EXE mapping first
  - DLL mapping/import binding second
  - shared module lifecycle unification last

## Minimal implementation plan

1. Add `ldr_user_bridge.h/.c` with session + user callback table.
2. Add context init/teardown APIs for user bridge.
3. Add `exec_user_program` path to invoke bridge for LRD0 EXE.
4. Keep existing manual loader path as fallback until parity checks pass.
5. Remove duplicate manual parser/mapping code after parity is proven.

## Validation targets

- `/bin/myldr-user_app.exe` loads and resolves imports from `/lib/myldr-user_lib.dll`.
- Existing legacy EXE and legacy DLL paths still work.
- Rollback works on malformed images and unresolved imports.
- No leaked pages on failed exec replacements.

## Post-integration fixes (Mar 2026)

### 1) Section-protect merge bug in user bridge

- Symptom: user EXE reached commit then faulted with instruction abort at entry PC.
- Root cause: page-protection merge misclassified read-only AP bits as writable because it compared only a subset of AP bits.
- Fix: in `ldr_user_merge_page_flags`, decode AP state with an AP mask (`(3 << 6)`) and compare exact AP value (`PE_AP_USER_RW`).
- Result: executable pages keep execute permission, writable pages stay writable, and later RO updates no longer corrupt mixed-page permissions.

### 2) Bridge runtime validation outcome

- `/bin/myldr-user_app.exe` now reaches `AppMain` and runs continuously.
- DLL import calls (`dll_add`, `dll_get_global_calls`, `dll_bump_local`) execute in loop without sync/data aborts in smoke runs.

### 3) Regression guard

- Added `tools/my-loader/smoke_user_app.sh` and make target `smoke-myldr-user-app`.
- The smoke checks assert:

  - Exec commit marker exists.
  - App reaches `AppMain` and prints step output.
  - No `SYNC_` or `DATA_ABORT` exception markers in captured log.
