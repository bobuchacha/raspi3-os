#!/usr/bin/env bash
set -euo pipefail

# Smoke-test my-loader user EXE + DLL bridge path in one command.
#
# What this checks:
# 1) Kernel image builds successfully.
# 2) Headless QEMU boots and shell can spawn myldr-user_app.
# 3) Exec commit log is emitted for /bin/myldr-user_app.exe.
# 4) App reaches AppMain and produces at least one progress line.
# 5) No synchronous/data abort markers appear in captured log.
#
# Usage:
#   tools/my-loader/smoke_user_app.sh
#
# Optional env overrides:
#   SMOKE_PROMPT_TIMEOUT  (default: 20)
#   SMOKE_RESULT_TIMEOUT  (default: 20)
#   SMOKE_LOG_PATH    (default: /tmp/rospi_smoke_myldr_user_app.log)

SMOKE_PROMPT_TIMEOUT="${SMOKE_PROMPT_TIMEOUT:-${SMOKE_BOOT_DELAY:-20}}"
SMOKE_RESULT_TIMEOUT="${SMOKE_RESULT_TIMEOUT:-${SMOKE_POST_DELAY:-20}}"
SMOKE_LOG_PATH="${SMOKE_LOG_PATH:-/tmp/rospi_smoke_myldr_user_app.log}"

# Ensure we run from repo root even when invoked from another directory.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${REPO_ROOT}"

source "${SCRIPT_DIR}/smoke_common.sh"

# `applications-vfs` remounts fat32.img before boot. If a stale QEMU instance is still holding the
# image open, macOS `hdiutil attach` fails with a generic "Resource temporarily unavailable" error.
# Fail early with a specific diagnostic so the cleanup action is obvious.
if command -v lsof >/dev/null 2>&1; then
  STALE_HOLDERS="$(lsof fat32.img 2>/dev/null | awk 'NR > 1 { print $1 " pid=" $2 }' || true)"
  if [[ -n "${STALE_HOLDERS}" ]]; then
    echo "[smoke] fat32.img is still in use; stop the stale emulator/process before retrying" >&2
    echo "${STALE_HOLDERS}" >&2
    exit 1
  fi
fi

make -j4 output/roskrnl

# Feed one shell command after the shell prompt appears and capture runtime log.
rm -f "${SMOKE_LOG_PATH}"

( wait_for_log_pattern "${SMOKE_LOG_PATH}" "ros:/\\$ " "${SMOKE_PROMPT_TIMEOUT}" "shell prompt" && printf "myldr-user_app\n" ) | \
  make run-headless > "${SMOKE_LOG_PATH}" 2>&1 &
SMOKE_RUN_PID=$!

if ! wait_for_log_pattern "${SMOKE_LOG_PATH}" "\[myldr-user_app\] step=" "${SMOKE_RESULT_TIMEOUT}" "my-loader user app progress"; then
  stop_smoke_run "${SMOKE_RUN_PID}"
  echo "[smoke] app produced no step output in ${SMOKE_LOG_PATH}" >&2
  tail -n 120 "${SMOKE_LOG_PATH}" >&2
  exit 1
fi

stop_smoke_run "${SMOKE_RUN_PID}"

# Required success markers for bridge startup and app execution.
if ! grep -q "Exec committed for /bin/myldr-user_app.exe" "${SMOKE_LOG_PATH}"; then
  echo "[smoke] missing exec commit marker in ${SMOKE_LOG_PATH}" >&2
  tail -n 120 "${SMOKE_LOG_PATH}" >&2
  exit 1
fi

if ! grep -q "\[myldr-user_app\] AppMain start" "${SMOKE_LOG_PATH}"; then
  echo "[smoke] app did not reach AppMain in ${SMOKE_LOG_PATH}" >&2
  tail -n 120 "${SMOKE_LOG_PATH}" >&2
  exit 1
fi

if ! grep -q "\[myldr-user_app\] step=" "${SMOKE_LOG_PATH}"; then
  echo "[smoke] app produced no step output in ${SMOKE_LOG_PATH}" >&2
  tail -n 120 "${SMOKE_LOG_PATH}" >&2
  exit 1
fi

# Abort markers that indicate bridge or mapping regressions.
if grep -Eq "Exception: (SYNC_|DATA_ABORT)" "${SMOKE_LOG_PATH}"; then
  echo "[smoke] detected abort marker in ${SMOKE_LOG_PATH}" >&2
  grep -nE "Exception:|SYNC_|DATA_ABORT" "${SMOKE_LOG_PATH}" >&2 || true
  exit 1
fi

echo "[smoke] PASS: my-loader user app bridge is healthy"
echo "[smoke] log: ${SMOKE_LOG_PATH}"
