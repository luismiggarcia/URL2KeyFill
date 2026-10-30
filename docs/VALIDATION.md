# Audio timing implementation — 30/10/2026

Additional implementation checks:

- Event-driven composition and immutable single-layer reuse: CTest 4/4 passed,
  including publication/visibility and concurrent configuration/destruction tests.
  The scene event test also passed under ThreadSanitizer without reported errors.
- CEF paint-rate requests now follow the selected mode (integer rate capped at 60).
  This is not fractional phase locking or physical judder acceptance.
- Core audio scheduling arithmetic is shared with the deterministic model; model
  outcomes are unchanged. The new uptime clock/gap detector foundation is tested but
  is not yet the application's active media clock.
- Real native/CEF monitor smoke passed: engine start, explicit configuration-handler
  rebuild, idle shutdown, PFL label, lower third and automatic AV diagnostic. Actual
  headphone/AirPlay changes and sleep/wake remain untested. The local 100 ms read
  offset and render callback allocations/locks are not resolved by lifecycle changes.
- User confirmed no loopback or joint A/V capture equipment is available yet;
  physical A/V offset and end-to-end latency acceptance remain pending.

P1 continuation: interval telemetry/CSV export and destination-owned PAUSE added.
CTest 3/3 passed, audit models and analyser aggregation/slope checks passed. The
10-second application hardware run passed smoke assertions, with zero missing
source frames, audio API errors or clock recoveries. CSV revealed **one dropped
video frame already in the first reporting interval**; this is not stability
acceptance. Evidence is in `tools/audio-audit/data/application-headroom-p1-short.csv.gz`.
The 30-minute baseline remains the next required measurement. Clock mapping and
scheduling reserves are still unchanged. The historical shared-mixer lifecycle
limitation described below has now been removed; monitor OUTPUT follows a separate
program-state flag, while PFL remains independent.

- Branch `codex/audio-timing`; final audit and hardware traces preserved first in commit `55dbb38`.
- Persistent project-local CMake 3.31.6 extracted from the archive with the SHA-256 recorded in BUILD.md. Full Release arm64 application rebuilt with real CEF and DeckLink; generated build files no longer depend on `/tmp` CMake.
- CTest passed 3/3, including new directed tests for source coverage, silent packets, internal gaps despite positive headroom, overlapping coverage, monitor isolation and channel routing.
- `tools/audio-audit/run-models.sh` passed. Timing/jitter/drift behaviour is intentionally unchanged in this slice.
- Native CEF/UI smoke test passed, including persistence, composition, lower third, automatic AV diagnostic and clean shutdown.
- HD Mini hardware smoke test passed: scheduled video/stereo audio, pause/resume, and STOP directly after PAUSE restoring the shared mixer. This is the minimal A1 lifecycle fix; moving PAUSE entirely out of the shared mixer remains pending.
- Audio controls now update audio separately; unchanged image configurations do not request recomposition. Persistent mutable commands use a 500 ms save debounce and normal shutdown flush. Transient monitor/transport commands do not request session writes.
- Diagnostics distinguish source coverage failures from silent samples and API errors from stream-clock recoveries. Per-layer headroom reports the last source read, not a filtered stability estimate; counters are cumulative per runtime. Missing coverage is summed per layer, not a unique count of missing output frames. Sources with no packet since creation/clear are classified separately internally. A source that silently stops without a CEF stream-stop notification is not automatically reclassified as inactive.
- Not yet performed: 30-minute application headroom recording, external A/V capture/listening, long-run stability or measured CPU benefit. This is an initial implementation slice, not completion/physical acceptance of P1 or the audit plan. Source clock, margin and three-frame scheduling reserve remain unchanged.

# Release 0.2.2 validation — 29/10/2026

- Release arm64 build with the real DeckLink backend passed; bundle version 0.2.2, build 4.
- Both CTest suites passed (2/2).
- AppKit/WKWebView/CEF smoke test passed with normal desktop access: UI controls, transparent composition, visibility, persistence, audio capture and clean shutdown. The restricted-shell launch could not start (exit 134); the desktop-access run passed.
- No-device integration test passed, including driver guidance and disabled PLAY.
- FILL/KEY selection was checked for stopped, playing and paused states; active-output warning and delete confirmation were checked separately with mocked DOM elements.
- User confirmed rounded window corners visually during this session. No new SDI output or receiver validation was performed for this release.
- Ad-hoc signature verified with `codesign --verify --deep --strict`; Apple notarization remains pending.

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
