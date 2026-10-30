#!/bin/bash
# Compile and run the mixer checks and the drift scenarios quoted in the audit.
# Needs only Xcode Command Line Tools; no CEF, DeckLink SDK or hardware.
#
# Usage: run-models.sh [output directory]   (binaries go there; default: a temporary directory)
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
here="$root/tools/audio-audit"
out="${1:-$(mktemp -d)}"; mkdir -p "$out"
core=("$root/src/core/Scene.cpp" "$root/src/core/Core.cpp")
c++ -std=c++20 -O2 -I"$root/src" "${core[@]}" "$here/mixer_checks.cpp" -o "$out/mixer_checks"
c++ -std=c++20 -O2 -I"$root/src" "${core[@]}" "$here/drift_sim.cpp" -o "$out/drift_sim"
trace="$out/cef-trace-120s.log"
gunzip -c "$here/data/cef-trace-2026-10-30-120s.log.gz" >"$trace"

echo "== Mixer checks"; "$out/mixer_checks"
echo; echo "== 3.1 Jitter: ideal clocks, measured lateness, margin 0.5 / 2 / 40 ms"
for m in 0.5 2 40; do "$out/drift_sim" --margin "$m" --seconds 117 --reports 1 --trace "$trace" | tail -1 | sed "s/^/margin $m ms: /"; done
echo; echo "== 3.2 Device 20 ppm fast (margin 3 ms, constant 1.4 ms lateness)"
"$out/drift_sim" --ppm 20 --margin 3 --seconds 900 --reports 6
echo; echo "== 3.2 Device 20 ppm slow, 8 h (500 ms history exhausted)"
"$out/drift_sim" --ppm -20 --margin 3 --seconds 28800 --reports 8

echo; echo "== Raw PTS: 60 ms source age, zero start margin, measured capture age"
"$out/drift_sim" --margin 0 --source-age-ms 60 --pts-domain raw --seconds 117 --reports 1 --trace "$trace" --max-missing-percent 0

echo; echo "== Calibrated raw PTS: 80 ms startup offset, 480-frame hardware FIFO"
"$out/drift_sim" --margin 80 --source-age-ms 60 --pts-domain raw --calibrate-clock 1 --fifo-frames 480 --seconds 117 --reports 1 --trace "$trace" --max-missing-percent 0

echo; echo "== Startup clock acquisition: 80 ms device stall, raw source timestamps"
"$out/drift_sim" --margin 0 --source-age-ms 60 --pts-domain raw --calibrate-clock 1 --startup-stall-ms 80 --seconds 117 --reports 1 --trace "$trace" --max-missing-percent 0
