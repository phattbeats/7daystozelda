#!/bin/bash
# Run a heavy build under a hard CPU cap. Usage: tools/limited-build.sh <build dir> [ninja args...]
# - refuses to start if another ninja/emcc build is already running (reuse it instead)
# - pins to 2 cores (the last two), nice 19, idle IO, ninja -j2
[ -n "$1" ] || { echo "usage: $0 <build dir> [ninja args]"; exit 2; }
DIR=$1; shift
for p in /proc/[0-9]*; do
  c=$(tr '\0' ' ' < $p/cmdline 2>/dev/null)
  case "$c" in
    *ninja*|*emcc*|*em++*) [ "${p#/proc/}" != "$$" ] && { echo "another build is running (pid ${p#/proc/}): wait for it and reuse it"; exit 1; } ;;
  esac
done
N=$(nproc); CPUS="$((N-2)),$((N-1))"
cd "$DIR" && exec nice -n 19 ionice -c3 taskset -c "$CPUS" ninja -j2 "$@"
