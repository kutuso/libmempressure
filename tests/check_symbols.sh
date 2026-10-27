#!/bin/sh
set -eu
lib="$1"
bad=$(nm -D --defined-only "$lib" | awk '{print $3}' | grep -v '^mp_' | grep -vE '^(_init|_fini|_edata|__bss_start|_end)$' || true)
if [ -n "$bad" ]; then
    echo "unexpected exported symbols:" >&2
    echo "$bad" >&2
    exit 1
fi
count=$(nm -D --defined-only "$lib" | grep -c ' T mp_')
[ "$count" -eq 8 ] || { echo "expected 8 public mp_ symbols, found $count" >&2; exit 1; }
