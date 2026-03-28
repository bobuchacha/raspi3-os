#!/usr/bin/env bash

SMOKE_FATAL_LOG_PATTERN_DEFAULT='Exception:|SYNC_INVALID_|IRQ_INVALID_|FIQ_INVALID_|ERROR_INVALID_|DATA_ABORT_ERROR|SYSCALL_ERROR|\[REGISTERS\]|ELR_EL1|ESR_EL1|FAR_EL1'

smoke_log_has_fatal_marker() {
  local log_path="$1"
  local failure_pattern="${2:-${SMOKE_FATAL_LOG_PATTERN_DEFAULT}}"

  if [[ ! -f "${log_path}" ]]; then
    return 1
  fi

  grep -Eiq "${failure_pattern}" "${log_path}"
}

smoke_log_has_reboot_marker() {
  local log_path="$1"
  local boot_count

  if [[ ! -f "${log_path}" ]]; then
    return 1
  fi

  boot_count="$(grep -Ec '^\[bootloader\] UART console ready$' "${log_path}" 2>/dev/null || true)"
  [[ -n "${boot_count}" ]] && [[ "${boot_count}" -ge 2 ]]
}

wait_for_log_pattern() {
  local log_path="$1"
  local pattern="$2"
  local timeout_seconds="$3"
  local description="$4"
  local failure_pattern="${5:-${SMOKE_FATAL_LOG_PATTERN_DEFAULT}}"
  local waited=0

  while [[ "${waited}" -lt "${timeout_seconds}" ]]; do
    if [[ -f "${log_path}" ]] && grep -Eq "${pattern}" "${log_path}"; then
      return 0
    fi
    if smoke_log_has_fatal_marker "${log_path}" "${failure_pattern}"; then
      echo "[smoke] detected early boot exception while waiting for ${description} in ${log_path}" >&2
      return 2
    fi
    if smoke_log_has_reboot_marker "${log_path}"; then
      echo "[smoke] detected an unexpected reboot while waiting for ${description} in ${log_path}" >&2
      return 3
    fi
    sleep 1
    waited=$((waited + 1))
  done

  echo "[smoke] timed out waiting for ${description} in ${log_path}" >&2
  return 1
}

stop_smoke_run() {
  local run_pid="$1"

  if [[ -z "${run_pid}" ]]; then
    return 0
  fi

  kill -TERM "${run_pid}" 2>/dev/null || true
  sleep 1
  kill -KILL "${run_pid}" 2>/dev/null || true
  wait "${run_pid}" 2>/dev/null || true
}