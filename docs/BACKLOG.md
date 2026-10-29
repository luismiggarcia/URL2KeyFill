# Development status — Studio Rack 0.2

## Implemented

- [x] Independent CEF URL layers, ordered premultiplied-alpha composition and persistent names, URLs, visibility, mute and gain.
- [x] Studio Rack English interface, four columns, stable monitor dimensions, fixed-height scrollable layer list and native background window dragging.
- [x] PLAY / PAUSE; LAYERS / EDIT LAYER; ADD LAYER / RELOAD; SETUP / unused key, with the agreed red/green/yellow states.
- [x] Visibility-only operation mode and editing/reordering mode. Hiding LAYERS disables editing; opening editing reveals the column without changing video visibility.
- [x] Layer drawer above Setup/diagnostics, mutually exclusive with Setup, click-to-toggle on the current layer, automatic field application and changed-URL reload.
- [x] Reload all, individual reload and confirmed layer deletion. New hidden layers use the built-in test URL.
- [x] Device refresh, valid saved-device priority, first-device selection and persistence, first-run single visible test layer, no-device setup guidance and SETUP SETTINGS title.
- [x] Browser size presets, custom width/height below Browser Size, independent viewport versus SDI target and proportional transparent fitting.
- [x] Real CEF audio capture, per-layer stereo mixing/gain/mute, bounded timestamped queues, limiting and scheduled SDI audio.
- [x] Confirmed policy: PAUSE silences the mix; hiding a layer silences that layer. Master audio mute is also available.
- [x] Asynchronous close of multiple browsers, driver failure handling, migration/atomic configuration, temporary incognito profiles and no personal keychain access.
- [x] Core/composition/audio tests, real CEF/UI integration and HD Mini scheduled-video/audio integration test.

## Still requires physical acceptance

- [ ] Observe both SDI Fill and Key simultaneously on a compatible switcher; check partial alpha, shadows, contour treatment, levels and colour with instruments.
- [ ] Listen/capture the embedded stereo SDI audio externally and measure channel mapping, distortion, pause transitions and A/V alignment.
- [ ] Long-duration and multi-mode tests, integer/fractional frame rates, repeated disconnect/reconnect and competing-application scenarios.
- [ ] Measure larger stacks and UHD output on compatible hardware. Two Full HD browser instances were used in the integration test; the 16-layer development guard is not a throughput guarantee.

## Distribution/platform work

- [ ] Developer ID signing, notarization, installer and clean-machine validation for public macOS distribution.
- [ ] Windows native host, trusted interface bridge, COM/device bootstrap and Windows sandbox validation.
- [ ] Additional pixel formats/interlaced field rendering if required by future devices.

The interface proposals are retained as design references. The installed 0.2 application now implements the operations above; physical acceptance items remain explicit and are not inferred from local preview or SDK counters.
