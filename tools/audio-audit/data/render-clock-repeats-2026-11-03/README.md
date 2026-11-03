# BeginFrame repeat investigation — 2026-11-03

1080p59.94, Reliable, UltraStudio HD Mini SDI loopback, `--external-begin-frame`.

- `proto-diag-a`, `proto-diag-b`: `run-loopback.py` 120 s with the A/V page and the
  unpainted/extra-paint counters (`RENDER_CLOCK` line in `playback.log.gz`).
- `probe-default`: 90 s with `tools/audio-audit/begin-frame-probe.html` and
  `--begin-frame-probe`; each paint decodes the rAF (BeginFrame) time it was produced for.
- `probe-all-stages`: same, with Chromium `run-all-compositor-stages-before-draw`
  (temporary build option, removed afterwards because it made association worse).

`analysis.json` is `analyze_begin_frame_probe.py` output. Lag 0 = the paint holds the
latest BeginFrame (the prototype's stamp); lag 1 = the BeginFrame before.
- `probe-accelerated-paint`: same probe with a temporary `--accelerated-paint` build
  (`shared_texture_enabled`, IOSurface copy in `OnAcceleratedPaint`, removed afterwards).
  `capture-timestamp.json` shows `extra.timestamp` jumps one frame with the pipeline
  regime like arrival time, and `capture_counter` counts paints: neither identifies the
  producing BeginFrame.
