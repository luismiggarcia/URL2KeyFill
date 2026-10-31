# Audio audit evidence

Reproducible evidence for the SDI audio timing findings in
[AUDITORIA-2026-10-30](../../docs/AUDITORIA-2026-10-30.md). Nothing here is part of the
application build; the tracked sources are never modified.

| File | Purpose |
|---|---|
| `run-models.sh` | Compiles and runs `mixer_checks` and the `drift_sim` scenarios quoted in the audit. Needs only the Command Line Tools (~10 s). |
| `mixer_checks.cpp` | Current behaviour behind A1, A4, A6, A7 and A8, using the real `AudioLane`/`AudioMixer`. |
| `drift_sim.cpp` | Deterministic model using the same `AudioSchedule` cursor/block arithmetic as `DeckLink.cpp`, with configurable device clock error, start margin, callback period and measured packet lateness. Prints the proposed "source headroom" diagnostic. |
| `build-instrumented.sh` | Builds a copy of the application whose CEF audio handler logs every packet (raw and adjusted PTS plus four clocks). Reuses an existing configured `build/` tree. |
| `instrument.py` | The patch applied to the *copied* `Browser.hpp` by `build-instrumented.sh`. |
| `capture-trace.sh` | Runs the instrumented copy with a throwaway home and temporary directory, then stops it. A window opens for the duration. |
| `analyze_trace.py` | Summarises a trace: PTS continuity, arrival lateness, misses per jitter margin, clock rates and the raw PTS clock domain. |
| `decklink_clock.cpp` | Plays transparent black and digital silence on the first DeckLink output and fits its stream clock, hardware reference clock and audio consumption against `mach_absolute_time`. **Puts a live signal on SDI OUT for the whole run.** |
| `data/*.log.gz` | CEF traces captured on 30/10/2026 (MacBook, Apple Silicon, built-in test layer). |
| `data/hdmini-clock-*.csv.gz` | Raw `decklink_clock` samples from the UltraStudio HD Mini, 30/10/2026. |

## Reproduce

```bash
tools/audio-audit/run-models.sh
```

```bash
python3 -I tools/audio-audit/analyze_trace.py <(gunzip -c tools/audio-audit/data/cef-trace-2026-10-30-120s.log.gz)
```

Device clock (requires Desktop Video and the SDK in `third_party/DeckLinkSDK`; run outside any shell sandbox, which hides the driver):

```bash
I=third_party/DeckLinkSDK/Mac/include; c++ -std=c++20 -O2 -I"$I" tools/audio-audit/decklink_clock.cpp "$I/DeckLinkAPIDispatch.cpp" -framework CoreFoundation -o /tmp/decklink_clock && /tmp/decklink_clock --seconds 600 --mode 1080p25 --csv /tmp/clock.csv
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

The ±20 ppm scenarios are stress cases. The measured HD Mini is far closer to the Mac:

| `decklink_clock` run (REF IN state not recorded) | Stream clock | Hardware clock | Audio consumption | Video frames late / dropped |
|---|---|---|---|---|
| 1080p25 BGRA, 600 s, 21 156 clean samples | +0.42 ppm | +0.42 ppm | +0.42 ppm | 0 / 0 of 14 999 |
| 1080p50 BGRA Fill/Key, 300 s, 10 176 clean samples | +0.28 ppm | +0.28 ppm | +0.28 ppm | 0 / 0 of 14 998 |

Fit residuals stay below 0.07 ms. The 60 s running fits moved from +0.51 to +0.28 ppm over
the 15 minutes, consistent with oscillator warm-up. All three device clocks agree, so
audio is consumed at the video clock rate. Positive means the device runs faster than
the Mac: the current mapping reads progressively later content and loses its forward
headroom at about 1.0–1.5 ms per hour.

`drift_sim --ppm 0.42` with the 120 s lateness trace (cycled, so its 30.7 ms outlier
recurs every 117 s; pessimistic for the jitter component):

| Start margin | 1 h | 2 h | 3 h | 4 h | 6 h |
|---|---|---|---|---|---|
| 3 ms | 0.022 % | 0.159 % | 1.708 % | 5.562 % | 20.370 % |
| 5 ms | 0.019 % | 0.021 % | 0.053 % | 0.958 % | 9.528 % |

On this unit, drift therefore becomes audible after hours rather than minutes; the
immediate risks are the missing source margin (3.1) and the wall-clock anchoring (3.3),
whose measured `system_clock` error (+3 to +12 ppm) is larger than the device error.
A genlocked unit follows its reference instead, so measure again with the reference
used in production.

Not covered: the real start margin δ inside the application's `DeckLink.cpp`, audible or
captured SDI audio, Core Audio monitor glitches and A1 inside `DeckLink.cpp`
(`mixer_checks` shows the mixer state it leaves behind).

## Application headroom recording (P1)

The application can export per-interval source coverage and headroom statistics
without logging from the audio callbacks. Set `U2K_AUDIO_TRACE` to a writable CSV
path. `--audio-baseline-seconds` runs the existing hardware smoke scenario for the
requested PLAY duration (1–28800 seconds), then exercises PAUSE/STOP and exits.
It emits real SDI video/audio. Run with normal desktop/driver access, outside the
execution shell sandbox, while interruption of the connected output is acceptable.

```sh
U2K_AUDIO_TRACE=/tmp/u2k-headroom.csv \
  ./build/URL2KeyFill.app/Contents/MacOS/URL2KeyFill --audio-baseline-seconds 1800
python3 -I tools/audio-audit/analyze_headroom.py /tmp/u2k-headroom.csv
```

This scenario uses the smoke test's 1080p25 selection and test layers, not the user's
saved session. Hardware availability is required. A zero exit code only means smoke
assertions passed; inspect the CSV for missing frames, recovery/API errors and video
late/drop counters before deciding stability. Missing frames are summed per lane;
this is not a unique count of missing program output frames. Empty headroom fields
mean no retained source sample. `no_source_reads` counts reads after creation/clear
before a first packet, separately from missing coverage in an established source.

Min/mean/max are collected for every SDI source read, reported about once per second
on the main thread. The mean is per read, not sample weighted. The analyser fits the
interval means after five seconds of warmup; this is a descriptive trend, not a
clock measurement or physical A/V sync test. Each capture should contain a single
continuous PLAY session. Keep exact binary commit, mode, driver, reference state,
load and elapsed recording time alongside the CSV. The initial interval can include
preroll and has a different headroom distribution from steady playback.

Recorded P1 evidence: `data/application-headroom-p1-1800s.*` contains the 30-minute
`bc8ae57` baseline and context/hash JSON; all monitored fault counters were zero.
`data/application-regression-*-10s.*` and the aggregate JSON cover candidate `34e9637`
at 25/50/59.94p. Audio counters were zero; 50p had one video drop in the first
reporting interval. These short runs do not establish prolonged candidate stability.
No physical A/V capture was available. See audit §9.5 for interpretation.

## Raw-clock candidate (P2)

The candidate in `build-audio-timing/` uses raw uptime PTS and a provisional 60 ms
source-age budget, independent of the three-frame device reserve. Override for
measurement only with `U2K_AUDIO_SOURCE_AGE_MS=10..200`. This is not a physical
output-latency control or an approved production default. Acquisition/correction
fields and the observed read age are in the extended CSV schema. Ignore acquisition
intervals when evaluating stable phase; retain all fault/padding counters.

Current instrumented traces keep raw PTS in both PTS columns. Analyse them with
`analyze_trace.py trace.log --pts-domain uptime`; historical adjusted traces still
use the default wall domain. `drift_sim --source-age-ms 60 --pts-domain raw` replays
capture age from the nine-column trace. `--calibrate-clock 1`, `--fifo-frames` and
`--startup-stall-ms` exercise the shared phase acquisition. The model's
`--max-missing-percent 0` turns source coverage failures into a failing exit status.

The source clock has not yet gained adaptive rate compensation. Initial phase
calibration does not establish prolonged drift correction or physical lip sync.

### Missing-read investigation

Application telemetry appends a persistent latest-gap snapshot (`gap_sequence` and
`gap_*`). Times are integer uptime samples at 48 kHz. It records the exact missing
read, retained newest packet end and arrival/publication times. The first subsequent
packet spanning a missing tail records recovery arrival/publication, even across
reporting intervals. Arrival precedes copying and acquiring the lane mutex; publication
is stamped after queue insertion/pruning, just before releasing it.

`analyze_headroom.py` reports read-end age and recovery delay. A packet arrival after
the read supports late delivery; arrival before but publication after points to work
or lock waiting inside publication. This is not a per-packet trace: only the latest
missing read is retained, sequence jumps expose overwritten reads, and recovery only
characterises a missing tail, not an internal gap. Generation prevents matching a
restarted stream. No file I/O or allocation is added to the missing-read diagnostic.
The extra packet clock reads have not yet been benchmarked on hardware.

### Physical SDI loopback A/V measurement

Connect **SDI Fill OUT → SDI IN** on the HD Mini. These probes require actual
simultaneous input/output support; they never change the duplex profile. Close
other owners of the input/output first. The capture tool enables only input and
cleans up only resources it enabled. It stores bounded in-memory measurements,
then writes CSV after stopping capture (no disk writes in input callbacks).

Build the two probes on macOS with the same SDK as the app:

```sh
clang++ -std=c++20 -O2 -I third_party/DeckLinkSDK/Mac/include \
  tools/audio-audit/decklink_capture.cpp third_party/DeckLinkSDK/Mac/include/DeckLinkAPIDispatch.cpp \
  -framework CoreFoundation -o /tmp/u2k-decklink-capture
clang++ -std=c++20 -O2 -I third_party/DeckLinkSDK/Mac/include \
  tools/audio-audit/decklink_clock.cpp third_party/DeckLinkSDK/Mac/include/DeckLinkAPIDispatch.cpp \
  -framework CoreFoundation -o /tmp/u2k-decklink-reference
```

Run `run-loopback.py` with `--capture /tmp/u2k-decklink-capture`, a **new**
`--directory`, `--mode 1080p25|1080p50|1080p5994` and `--seconds 30`, plus either:

- `--app /absolute/path/URL2KeyFill.app/Contents/MacOS/URL2KeyFill`: runs the
  actual app with `--sdi-av-test`, one bundled sync source, temporary smoke
  settings and the normal mixer/compositor/output. No saved production session is loaded.
- `--reference /tmp/u2k-decklink-reference`: schedules white video for 40 ms and
  a stereo 1 kHz pulse with a 2 ms envelope every second on the SDK output timeline,
  without CEF or the app mixer. Its A/V-reference mode starts playback from the audio
  preroll callback only after the SDK reports enough buffered audio and video, as in
  the bundled SDK SignalGenerator example. This is a comparison of the combined output/input
  path, not an independent calibration of input alone. Compare the same mode and
  repeat after restarts; do not assume an identical offset across modes.

By default the runner starts playback and waits 4 s before enabling input;
`--settle-seconds` accepts 1–10 s. Input first warms until one second of consecutive
valid audio/video (six-second deadline), then stops, flushes and restarts with reset
measurement counters. READY is emitted only after this sequence. The runner ends
recording one second before playback is due to stop, avoiding output reconfiguration
inside the measurement. At least eight seconds of recording must remain; use 30 s
or longer (at least 25 s recommended). Any recorded no-signal event invalidates the
measurement. The probe disables recording before its own StopStreams.
The runner waits for input readiness, hashes both executables, captures exit codes,
and saves logs, timestamps, per-pulse results and app telemetry. `analyze_loopback.py`
can also analyse a captured CSV directly. Positive `audio_minus_video_ms` means
sound follows the detected white frame. Luma threshold is 200 in the centre patch;
audio onset is the first sample above .003 FS, rearmed after 100 ms of quiet.
The envelope and threshold introduce a small onset bias also present in the SDK
reference. Both SDK input timestamps use a 48 kHz scale; callback arrival times are
not used as media times. The input format is fixed 8-bit YUV with stereo 32-bit PCM.
Input selection is left at the device's existing connection setting (must be SDI).

This records measurement features, not full pictures or raw sound. It is **relative
A/V through output plus input**, not host-to-wire latency. The browser visual flash
is frame-quantised and its generation is not a laboratory reference. The existing
40 ms flash can be missed by CEF painting at 25 fps; captures with fewer than five
clean paired flashes after warmup are rejected. Use the default 30 s or longer;
shorter runs can legitimately fail this quality check. A 1 Hz pattern
has cycle ambiguity; nearest pairing assumes an offset within 400 ms. Global input
timestamp gaps include startup/stop; the analyser separately reports gaps within the
matched pulse range. This same-device test does not validate independent clock drift.
The short clock-rate fits printed by the SDK reference are not used as ppm evidence.

Validate the analyser with `PYTHONDONTWRITEBYTECODE=1 python3 tools/audio-audit/test_loopback.py`.

### Startup readiness telemetry

`preroll_audio_frames` / `preroll_video_frames` are the SDK-reported queue counts
used to authorise StartScheduledPlayback from the actual audio preroll callback.
They are not estimated counts of writes accepted by ScheduleAudioSamples. Normal
reserves remain three video frames and the existing audio target. A five-second
preroll-readiness wait returns a controlled failure; fatal callback errors wake the waiter.
The video worker is waiting before the first possible frame completion.

`first_dropped_frame` is the zero-based scheduled frame index of the first SDK drop,
recorded through the three reusable frame slots. -1 means absent/unknown; always
consult dropped_video too. Frame pointer/index metadata uses atomics and the capture
diagnostic does not allocate or make extra driver calls in the video callback.
`analyze_headroom.py` includes these optional fields without changing old CSV results.
Startup readiness and content A/V phase remain separate acceptance criteria.

The SDK reference also accepts `--video-preroll 3..8` and
`--audio-preroll 480..12000` in `--av-reference 1` mode. Defaults remain 4 frames /
9600 audio samples; `--video-preroll 3 --audio-preroll 2880` matches the app's
50p reserve. These settings are for diagnostic comparison, not new app defaults.
The app now enables audio output before scheduling any video, matching the SDK
SignalGenerator ordering, in addition to starting from actual preroll readiness.

Before scheduled preroll, the app sends one transparent-black frame with
DisplayVideoFrameSync to initialise the physical video path. This is an explicit
startup frame, not a scheduled programme frame: `startup_sync_frames` and
`startup_sync_ms` report the successful preparation and duration separately. It
reuses one of the three video slots, with no additional steady-state queue or DSP.
If preparation fails, startup fails and owned resources are released. This is not
a fixed sleep or a dropped programme frame removed from the counter.

`startup_lead_frames` declares the initial programme lead (one frame). Programme
frame 0 is scheduled at that future instant, and **still has programme index 0**
in first_dropped_frame diagnostics. No programme frame is removed or drop counter
suppressed. `startup_audio_padding_frames` counts actual scheduled initial silence
(960 samples at 50p; 1920 at 25p). This pads the same startup interval before source
PCM, with separate accounting from pre_source_padding_frames. Partial writes stop
exactly at the boundary. Phase acquisition waits until the initial padding and two
queue depths have been consumed; after acquisition the normal source-age budget
still applies. This adds an explicit startup interval, not a permanent extra queue.
Fractional frame intervals round initial audio padding up to the next sample.

### Temporal video selection and diagnostic decomposition

The output selects bounded historical video on the audio source mapping; preview
continues to show the latest frame. SDR pixels are prepared before scheduling and
released on STOP. See `docs/ARCHITECTURE.md` for the 16-frame / 192–384 MiB bounds,
half-frame selection tolerance and scoped macOS activity. No accelerated IOSurface
path or empirical fixed A/V offset is enabled.

`analyze_pipeline_sync.py application.csv --capture-result result.json` compares
source flash arrival versus audio PTS, scheduled A/V and measured A/V. These source
and scheduled markers are enabled only by `--sdi-av-test`. A stable calibrated audio
origin and at least five matched events are required; multiple origins are rejected.
Paint arrival is not content PTS. Snapshot windows can omit events, so differing
phase clusters produce a conservative difference range rather than a subtraction
of medians presented as transport delay. Input latency remains included.

Run `PYTHONDONTWRITEBYTECODE=1 python3 tools/audio-audit/test_pipeline_sync.py`
(four tests), alongside `test_loopback.py` (nine tests). Final evidence, rejected
experiments and fixed-output/input-restart controls are under
`data/video-timeline-2026-10-31/`; acceptance remains partial as explained in audit §10.9.
