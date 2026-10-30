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
