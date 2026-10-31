# Versions

## 0.1.0

The original macOS application is preserved as a ZIP in [Release 0.1.0](https://github.com/luismiggarcia/URL2KeyFill/releases/tag/0.1.0), with a SHA-256 checksum. Its exact source code was not recovered. The 0.1.0 commit and tag contain only the preserved application record, not reconstructed sources.

## 0.2.0

The first version with complete source code in this history. Introduces the Studio Rack interface, web-layer composition, stereo SDI audio and persistent configuration. See VALIDATION.md for performed tests and physical validation limits.

## 0.2.1

English-only project documentation, built-in test page and remaining project-owned test/comment text. Application metadata identifies version 0.2.1. The macOS arm64 release supports Apple Silicon, including M1; it is not an Intel or Universal build. Video/audio behavior is unchanged from 0.2.0. The application is ad-hoc signed, not Apple notarized.

## 0.2.2

Rounded native window corners, including the footer, and window dragging from empty layer-list space. Quit immediately when SDI output is stopped; show a prominent interruption warning while output is active, including PAUSE. Rename IMAGE to FILL and keep FILL/KEY selection amber independently of PLAY. Release for macOS Apple Silicon (arm64), ad-hoc signed and not Apple notarized.

## 0.4.0

Dedicated AUDIO drawer with horizontal layer scrolling and a fixed output strip.
Vertical gain faders, stereo meters, independent L/R channel switches, mono and mute
controls for layers and output. Local monitoring supports multiple layers together
or the output exclusively, without consuming SDI audio samples. The layer editor
retains its audio controls, adds L/R switches and a horizontal source meter.
The monitor-side meter fills the space below KEY and supports double-click mute.
Command-W uses the existing guarded quit flow; RELOAD is now RELOAD LAYERS.

Validation: core and scene tests, application build, and native CEF/UI smoke test.
Physical SDI output and audible local-monitor playback require hardware/listening
validation. macOS Apple Silicon (arm64), ad-hoc signed and not Apple notarized.

## 0.5.0

- Adaptive audio/video clock mapping to mitigate SDI clock drift, with bounded rate correction and a high-quality sinc audio resampler.
- Live signed A/V calibration in Advanced Setup (-200 to +200 ms), persisted without restarting playback.
- Reliable default profile and experimental Low latency profile. At 25p, the configured internal budget falls from 180 to 130 ms; at 50p, from 120 to 90 ms. These budgets exclude source and receiver buffering and calibration offsets.
- Reduced processing for common cases: silent audio fast path, single-layer composition reuse and preparation outside the output deadline.
- Expanded diagnostics, physical SDI loopback probes and an endurance runner with scoped sleep prevention and continuity/synchronization checks.

Validation: native build, CTest (5/5), Python tests (46/46), sanitizer checks, SDI loopback at 25/50/59.94 fps, live calibration and a calibrated 120-second endurance rehearsal. Eight-hour clock simulations passed; the physical eight-hour endurance run remains pending. Calibration is source/session dependent: these results do not guarantee lip sync for every URL. Local-monitor timing redesign and subjective validation with real sources remain pending. See docs/AUDITORIA-2026-10-30.md for measurements and limitations.

macOS Apple Silicon (M1 and later), macOS 13 or later. Real DeckLink backend. Ad-hoc signed; not Apple notarized.

Download the ZIP, extract it and open URL2KeyFill.app. SHA256SUMS.txt contains the archive checksum.

## 0.5.1

- ADD LAYER shows the layer list, enables EDIT LAYER and opens the newly created layer drawer after successful creation.
- POWER uses an inline SVG symbol with the green LED illumination prepared in the standalone studio-button component.

Validation: native application build, JavaScript syntax check and CTest (5/5). The no-device UI/CEF smoke test reported all checks as PASS but exited with status 1; a clean smoke-test exit remains unverified.

## 0.5.2

- Shared gain sliders/faders with clickable dB markers, aligned scales and double-click reset to 0 dB.
- Unified stereo audio meters across the main panel, layer buttons, layer editor and audio drawer. Layer buttons use the approved 17 × 33 px compact meter; the editor uses a horizontal meter.
- Consistent meter backgrounds, colour zones, peak indication and mono/mute states, retaining the existing RMS calibration and native gain routing.
- Includes the unpublished 0.5.1 improvements: ADD LAYER opens the new layer for editing, and the POWER symbol uses green LED illumination.

Validation: native Release arm64 build with DeckLink enabled; CTest (5/5); browser integration checks with a simulated native bridge, including gain routing, all horizontal markers, numeric dB entry, reset, mono/mute, RMS calibration and layout at 1050 and 1280 px. No new physical SDI or listening validation was performed for this UI release.

macOS Apple Silicon (M1 and later), macOS 13 or later. Ad-hoc signed; not Apple notarized. The ZIP includes the application; SHA256SUMS.txt contains its checksum.
