# Development status — URL2KeyFill 0.7

## Implemented

- [x] Independent CEF URL layers, ordered premultiplied-alpha composition and persistent names, URLs, visibility, mute and gain.
- [x] Studio Rack English interface, four columns, stable monitor dimensions, fixed-height scrollable layer list and native background window dragging.
- [x] PLAY / PAUSE; EDIT LAYERS / ADD LAYER (only while editing); RELOAD LAYERS / blank key; SETUP / AUDIO, with the agreed red/green/yellow states and unified status LEDs.
- [x] Layer drawer above Setup/diagnostics, reload all/individual, deletion confirmed by a second press, device refresh and persistence, browser size presets and proportional transparent fitting.
- [x] Always-visible layer list with a visibility-only operation mode and an editing/reordering mode.
- [x] View menu: Rack Size, Small Size (75%), Tiny Size (50%), Just Monitor and Always on Top; About panel and version in SETUP.
- [x] Local confirmations instead of dialogs: POWER asks for a second press outside PLAY and refuses to quit in PLAY (blinking ON AIR), also for ⌘Q/⌘W.
- [x] Real CEF audio capture, per-layer stereo mixing/gain/mute, bounded timestamped queues, limiting and scheduled SDI audio.
- [x] Confirmed policy: PAUSE silences the mix; hiding a layer silences that layer. Master audio mute is also available.
- [x] Asynchronous close of multiple browsers, driver failure handling, migration/atomic configuration, temporary incognito profiles and no personal keychain access.
- [x] Core/composition/audio tests, real CEF/UI integration and scheduled-video/audio integration tests.
- [x] SDI audio clock servo with a recovery budget (the output fails only on the third recovery within 10 minutes).
- [x] CEF audio streams are accepted only when their PTS is in the host uptime domain; rejected streams are shown in diagnostics.
- [x] SDI video history kept across on-air layer changes (no blackout when a layer is shown, hidden, added or reordered).
- [x] Per-mode calibrated A/V default (25p +17 ms, 50p +14 ms, 59.94p −2 ms) with a manual A/V adjustment on top.
- [x] Reliable profile: at least 100 ms of SDI audio queued, `max(3 frames, 100 ms)` of video in flight, and top-up by the device clock with spare slots.
- [x] Bundled A/V diagnostics page, standalone layer tests and loopback measurement tools in `tools/audio-audit/`.

## Physically verified (UltraStudio, SDI OUT → SDI IN loopback)

- [x] 8-hour continuity at 1080p25 (0.5.0) and at 1080p59.94 (Reliable, 2026-11-03): no late or dropped frames, no missing audio samples, no recoveries.
- [x] A/V offset by loopback capture at 25p, 50p and 59.94p, single layer and two layers; limits and results in `AUDITORIA-2026-10-30.md` §11.4–11.6.

## Still requires physical acceptance

- [ ] Observe both SDI Fill and Key simultaneously on a compatible switcher; check partial alpha, shadows, contour treatment, levels and colour with instruments.
- [ ] Listen to the embedded stereo SDI audio on an external receiver (channel mapping, distortion, pause transitions).
- [ ] Long-duration run at 1080p50, and multi-layer long sessions.
- [ ] An SDI test that forces a servo recovery (the recovery budget is covered by unit tests only).
- [ ] Repeated disconnect/reconnect and competing-application scenarios.
- [ ] Larger stacks and UHD output on compatible hardware. The 16-layer development guard is not a throughput guarantee.

## Deferred engineering

- [ ] Real-time hygiene in `RenderAudioSamples`: pre-allocate the mix buffers and avoid taking lane/mixer mutexes in the DeckLink callback (measured maximum 0.333 ms, not urgent).
- [ ] External BeginFrame prototype (`--external-begin-frame`, off by default): repeat the `begin-frame-probe` when [CEF#4317](https://github.com/chromiumembedded/cef/pull/4317) ships in a CEF release, then decide whether to enable it.
- [ ] Per-source A/V timing contracts (audit §10.19), needed for certified A/V rather than a calibrated default.
- [ ] Multi-layer composition timestamp: it follows the most recent layer paint, which widens A/V dispersion with animated overlays.

## Distribution/platform work

- [ ] Developer ID signing, notarization, installer and clean-machine validation for public macOS distribution.
- [ ] Windows native host, trusted interface bridge, COM/device bootstrap and Windows sandbox validation.
- [ ] Additional pixel formats/interlaced field rendering if required by future devices.

## Operational notes

- Disable automatic updates (OneDrive included) on a broadcast machine during service.
- If the output shows out-of-place frames after the UltraStudio is reconnected or the Mac wakes, restart the Mac before service.

Physical acceptance items remain explicit and are not inferred from local preview or SDK counters.
