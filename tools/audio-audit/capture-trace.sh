#!/bin/bash
# Run the instrumented application with a throwaway profile and capture a CEF audio trace.
# A new installation opens one visible test layer, whose page plays a continuous stereo loop.
#
# Usage: capture-trace.sh <work directory> <seconds> <trace file>
set -euo pipefail
work="$1"; seconds="$2"; trace="$3"
app="$work/URL2KeyFill.app/Contents/MacOS/URL2KeyFill"
home="$work/home" temp="$work/tmp"; rm -rf "$home" "$temp"; mkdir -p "$home" "$temp"; rm -f "$trace"
# CFFIXED_USER_HOME keeps session.json away from the real Application Support folder;
# TMPDIR keeps the temporary CEF profile inside the work directory after a forced stop.
CFFIXED_USER_HOME="$home" TMPDIR="$temp" U2K_TRACE="$trace" "$app" >"$work/app.log" 2>&1 &
pid=$!
sleep "$seconds"
kill -TERM "$pid" 2>/dev/null || true
for _ in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
kill -KILL "$pid" 2>/dev/null || true
pkill -KILL -f "$work/URL2KeyFill.app/Contents/Frameworks" 2>/dev/null || true
echo "$(wc -l <"$trace") packets in $trace"
