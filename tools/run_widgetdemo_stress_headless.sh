#!/bin/sh

set -eu

# Drive the userspace shell with a repeatable widgetdemo launch burst.
#
# The loader/resource rewrite is intended to remove fixed pool ceilings during
# repeated GUI process creation. This helper keeps the host-side test input
# deterministic so the resulting QEMU log is directly comparable across runs.

COUNT="${1:-100}"
BOARD="${BOARD:-virt}"
LOG_PATH="${LOG_PATH:-/tmp/widgetdemo_stress.log}"

if [ "$COUNT" -le 0 ] 2>/dev/null; then
    echo "count must be a positive integer" >&2
    exit 1
fi

{
    printf '\n'
    index=0
    while [ "$index" -lt "$COUNT" ]; do
        printf 'start widgetdemo\n'
        index=$((index + 1))
    done
} | make BOARD="$BOARD" run-virt-headless > "$LOG_PATH" 2>&1 || true

echo "widgetdemo stress log: $LOG_PATH"
