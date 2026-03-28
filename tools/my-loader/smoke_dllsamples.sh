#!/usr/bin/env bash
set -euo pipefail

SMOKE_PROMPT_TIMEOUT="${SMOKE_PROMPT_TIMEOUT:-${SMOKE_BOOT_DELAY:-20}}"
SMOKE_RESULT_TIMEOUT="${SMOKE_RESULT_TIMEOUT:-${SMOKE_POST_DELAY:-20}}"
SMOKE_LOG_PATH="${SMOKE_LOG_PATH:-/tmp/rospi_smoke_dllsamples.log}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${REPO_ROOT}"

source "${SCRIPT_DIR}/smoke_common.sh"

if command -v lsof >/dev/null 2>&1; then
  STALE_HOLDERS="$(lsof fat32.img 2>/dev/null | awk 'NR > 1 { print $1 " pid=" $2 }' || true)"
  if [[ -n "${STALE_HOLDERS}" ]]; then
    echo "[smoke] fat32.img is still in use; stop the stale emulator/process before retrying" >&2
    echo "${STALE_HOLDERS}" >&2
    exit 1
  fi
fi

make -j4 output/roskrnl output/applications/programs/dllsamples.exe output/applications/dll/alpha.dll

rm -f "${SMOKE_LOG_PATH}"

( wait_for_log_pattern "${SMOKE_LOG_PATH}" "ros:/\\$ " "${SMOKE_PROMPT_TIMEOUT}" "shell prompt" && printf "dllsamples\n" ) | \
  make run-headless > "${SMOKE_LOG_PATH}" 2>&1 &
SMOKE_RUN_PID=$!

if ! wait_for_log_pattern "${SMOKE_LOG_PATH}" "\[dllsamples\] bridge -> alpha result=" "${SMOKE_RESULT_TIMEOUT}" "dllsamples completion"; then
  stop_smoke_run "${SMOKE_RUN_PID}"
  echo "[smoke] dllsamples did not complete bridge -> alpha call in ${SMOKE_LOG_PATH}" >&2
  tail -n 120 "${SMOKE_LOG_PATH}" >&2
  exit 1
fi

stop_smoke_run "${SMOKE_RUN_PID}"

if ! grep -q "\[dllsamples\] alpha entry=" "${SMOKE_LOG_PATH}"; then
  echo "[smoke] dllsamples did not report alpha entry output in ${SMOKE_LOG_PATH}" >&2
  tail -n 120 "${SMOKE_LOG_PATH}" >&2
  exit 1
fi

if ! grep -q "\[dllsamples\] bridge -> alpha result=" "${SMOKE_LOG_PATH}"; then
  echo "[smoke] dllsamples did not complete bridge -> alpha call in ${SMOKE_LOG_PATH}" >&2
  tail -n 120 "${SMOKE_LOG_PATH}" >&2
  exit 1
fi

if grep -Eq "Exception: (SYNC_|DATA_ABORT)" "${SMOKE_LOG_PATH}"; then
  echo "[smoke] detected abort marker in ${SMOKE_LOG_PATH}" >&2
  grep -nE "Exception:|SYNC_|DATA_ABORT" "${SMOKE_LOG_PATH}" >&2 || true
  exit 1
fi

echo "[smoke] PASS: dllsamples resolved mixed legacy/LRD0 DLLs"
echo "[smoke] log: ${SMOKE_LOG_PATH}"