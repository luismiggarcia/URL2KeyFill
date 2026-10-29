# Release 0.2.1 validation — 29/10/2026

- Project-owned tracked text checked for remaining Portuguese; documentation and the built-in test page are now English.
- Release build and both CTest suites passed (2/2).
- Packaged application passed the real AppKit/WKWebView/CEF smoke test, including layer composition, persistence, UI controls and 31,200 captured stereo audio samples. No SDI output was started for this release check.
- Executable architecture: arm64; Mach-O minimum macOS version: 13.0. This is an Apple Silicon build, including M1, rather than a physical test on every supported Mac model.
- Ad-hoc signature verified with `codesign --verify --deep --strict`. Apple notarization remains pending.

# Validation update — 29/10/2026, Studio Rack 0.2

- Installed version 0.2.0 in `~/Applications/URL2KeyFill.app`, preserving a hidden backup of the prior installed app. The installed copy passed the full non-SDI integration test and signature verification.
- No-device test passed with an explicitly test-only empty device list: SETUP driver help appeared and PLAY was disabled; real CEF rendering/audio capture continued. This test does not simulate successful hardware playback.
- Full Release arm64 application compiled with real CEF and DeckLink backend; local signature verification passed.
- CTest: 2/2 passed. Composition tests cover ordering, premultiplied alpha, transparent fitting, visibility, stereo gain/mixing, limiting, mute and bounded queues.
- Real AppKit/WKWebView/CEF integration passed: bridge, monitor geometry with layer column hidden/visible, mutually exclusive drawers, edit drawer toggle, custom dimensions and PAUSE guard. These checks evaluate the local UI DOM; no screen recording permission was used.
- Two independent Full HD transparent browser instances composed correctly; hiding one returned the expected alpha. Layer configuration persisted and CEF closed cleanly.
- CEF generated/captured real planar stereo audio at 48 kHz. A monotonic-versus-epoch timestamp mismatch was found and corrected before audio acceptance.
- HD Mini hardware integration passed at 1080p25: scheduled video completions, scheduled stereo audio, more than 4800 non-zero mixed PCM samples, PAUSE/resume and stop. The test finished with exit code 0.
- Timed integrated run: 8.47 seconds wall time, 2.95 seconds user CPU and 1.60 seconds system CPU. `time -l` reported maximum RSS 330,366,976 bytes (about 315 MiB); this is not a sum of the live memory of all CEF/WebKit helpers. It is a short integration run, not a long-duration benchmark.
- Chromium emitted an ad-hoc process validation-category warning on startup. `codesign --verify --deep --strict` passed and the test completed; Developer ID/notarized distribution remains pending.

No receiver loopback or new screen capture was performed. SDK acceptance and local non-zero PCM do not establish audible reception, measured A/V sync, physical Key correctness or production readiness. The remaining physical protocol is in HARDWARE.md and BACKLOG.md. No physical UHD claim is made.

---

# Historical validation — 2026-10-28

## Results before Studio Rack

- Full Release arm64 build with CEF and the DeckLink backend passed.
- CTest: 1/1 passed, covering settings, rational timing, geometry, alpha, stride, buffer ownership, pipeline and URL policy.
- The real graphical `--smoke-test` passed with exit code 0 outside the execution tool's shell sandbox. Chromium's internal sandbox remained enabled.
- CEF 1920 × 1080 pixel checks passed for transparent, opaque and partially transparent pixels, BGRA order and orientation.
- A 640 × 480 viewport fitted Full HD proportionally with transparent side margins.
- A 3840 × 2160 viewport scaled to Full HD passed. This did not validate physical UHD SDI.
- Settings persistence, load-error image clearing and complete CEF shutdown passed.
- `codesign --verify --deep --strict` passed after removing Finder metadata added by OneDrive. Synchronization can reintroduce those attributes.
- At the initial probe, the real driver reported no compatible SDI output device. No physical output was started during that probe.

## Keychain and browser data

The earlier Chromium configuration attempted to access Chromium Safe Storage. Before retesting, the application was changed to use `use-mock-keychain`, an exclusive temporary root and an in-memory profile. It does not use a personal browser profile, require a keychain password or persist web cookies/authentication. The test then completed without the earlier blocking behavior. No system-call audit was performed; this conclusion is based on configuration and functional testing.

The flag is documented by [GoogleChrome chrome-launcher](https://github.com/GoogleChrome/chrome-launcher/blob/main/docs/chrome-flags-for-tools.md). In-memory profile behavior is described in the pinned CEF `cef_types.h` fields `cache_path` and `root_cache_path`.

## Hardware recognition and 1080p60 enumeration

After the BlackmagicIO and Network extensions were authorized, the SDK detected the UltraStudio HD Mini without a restart. The user supplied a photograph of the test image on an external monitor and confirmed image output. Physical Key output and switcher compositing remained unverified because the necessary receiving connections were unavailable.

Mode enumeration was corrected to query ordinary output and Keying independently. The attached device with driver 16.4 rejected BGRA/ARGB ordinary playback at 1080p50/59.94/60 but accepted them with `bmdSupportedVideoModeKeying`. The original filter discarded these modes before the Keying query. Rebuilding and querying the real device confirmed all three rates in the list, in BGRA and ARGB. This established enumeration support, not long-duration playback validation at every rate.

## Remaining acceptance work

Follow HARDWARE.md to validate simultaneous Full HD Fill/Key, levels/color, shadows/edges, integer/fractional rates, static-page SDI continuity, pause/resume, restart and reconnection. Local preview does not prove these physical properties.

Physical UHD and prolonged performance tests require compatible equipment. The Windows host remains unimplemented. The application is not certified for production.
