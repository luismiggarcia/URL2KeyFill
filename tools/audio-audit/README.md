# Audio audit evidence

Reproducible evidence for the SDI audio timing findings in
[AUDITORIA-2026-10-30](../../docs/AUDITORIA-2026-10-30.md). Nothing here is part of the
application build; the tracked sources are never modified.

| File | Purpose |
|---|---|
| `run-models.sh` | Compiles and runs `mixer_checks` and the `drift_sim` scenarios quoted in the audit. Needs only the Command Line Tools (~10 s). |
| `mixer_checks.cpp` | Current behaviour behind A1, A4, A6, A7 and A8, using the real `AudioLane`/`AudioMixer`. |
| `drift_sim.cpp` | Deterministic model of `AudioCallback` (exact arithmetic from `DeckLink.cpp`) with configurable device clock error, start margin, callback period and measured packet lateness. Prints the proposed "source headroom" diagnostic. |
| `build-instrumented.sh` | Builds a copy of the application whose CEF audio handler logs every packet (raw and adjusted PTS plus four clocks). Reuses an existing configured `build/` tree. |
| `instrument.py` | The patch applied to the *copied* `Browser.hpp` by `build-instrumented.sh`. |
| `capture-trace.sh` | Runs the instrumented copy with a throwaway home and temporary directory, then stops it. A window opens for the duration. |
| `analyze_trace.py` | Summarises a trace: PTS continuity, arrival lateness, misses per jitter margin, clock rates and the raw PTS clock domain. |
| `data/*.log.gz` | Traces captured on 30/10/2026 (MacBook, Apple Silicon, built-in test layer). |

## Reproduce

```bash
tools/audio-audit/run-models.sh
```

```bash
python3 -I tools/audio-audit/analyze_trace.py <(gunzip -c tools/audio-audit/data/cef-trace-2026-10-30-120s.log.gz)
```

New trace (requires the application build and a desktop session):

```bash
tools/audio-audit/build-instrumented.sh /tmp/u2k-audit && tools/audio-audit/capture-trace.sh /tmp/u2k-audit 120 /tmp/u2k-audit/trace.log
```

## Results recorded on 30/10/2026

Traces: `65s` (first audit run, 6549 packets, 6-column format without raw PTS) and
`120s` (11 743 packets, full format).

| Measurement | 65 s trace | 120 s trace |
|---|---|---|
| PTS step | 10 ms for every packet | 10 ms for every packet |
| Overlaps / gaps at 48 kHz | 0 / 0 | 0 / 0 |
| Arrival after adjusted packet start, p50 / p99 / max | 1.37 / 2.14 / 17.57 ms | 0.95 / 1.60 / 30.70 ms |
| Packets late for a read ending at now − 2 ms | 4.37 % | 0.03 % |
| Packets late for a read ending at now − 20 ms | 0 | 2 |
| Packets late for a read ending at now − 40 ms | 0 | 0 |
| `system_clock` against `steady_clock` | +11.9 ppm | +3.3 ppm |

Raw CEF PTS clock domain (120 s trace): `mach_absolute_time − raw PTS` stays between
21.0 and 22.0 ms (1 ms truncation), rate difference −6.6 ppm, within quantisation. The
pinned CEF build therefore stamps packets on `mach_absolute_time` (uptime, stops
during sleep), about 21.5 ms before they reach the handler, despite the header's
Unix-epoch description. libc++ `steady_clock` equals `mach_continuous_time` (includes
sleep) on this system: it differed from the PTS domain by 24 452 s of accumulated sleep.
**`steady_clock` is not a drop-in replacement**; a common timeline must use
`mach_absolute_time` (or `CLOCK_UPTIME_RAW`) or calibrate the offset explicitly, and
treat sleep/wake as a discontinuity.

`drift_sim` (callback period 9.7 ms so the callback phase sweeps across packet arrivals):

| Scenario | Result |
|---|---|
| Ideal clocks, measured 120 s lateness, margin 0.5 / 2 / 40 ms | 0.178 % / 0.024 % / 0 % samples missing |
| Device +20 ppm, margin 3 ms | losses start once drift exceeds margin − lateness (≈1.6 ms, ~80 s); 4.5 % missing at 5 min, 41 % at 10 min, 87 % at 15 min |
| Device −20 ppm, margin 3 ms | headroom (latency) grows 72 ms/h; losses from ~7 h, near silence at 8 h |

The device clock error of the HD Mini has not been measured. The headroom slope printed
by `drift_sim` is the quantity to log on hardware: its rate in ms per hour is the device
error in ppm × 3.6, measurable within minutes.

Not covered: physical SDI output, the real DeckLink start margin, Core Audio monitor
glitches and A1 inside `DeckLink.cpp` (needs the SDK; `mixer_checks` shows the mixer
state it leaves behind).
