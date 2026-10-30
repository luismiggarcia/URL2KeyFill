# URL2KeyFill

macOS Apple Silicon application: **independent web layers → transparent CEF composition → Blackmagic SDI Fill + Key and stereo audio**.

## Open the application

Use the **URL2KeyFill** Desktop shortcut or `~/Applications/URL2KeyFill.app`. The local development build is `build/URL2KeyFill.app`. The Studio Rack interface is entirely in English.

## Operation

- **PLAY** starts/stops physical SDI output. Red means on air; green means playing but paused.
- **PAUSE** cuts image to transparent and silences audio while maintaining the SDI signal. It is only available during PLAY.
- **LAYERS** shows/hides the layer column without resizing the monitor. Hiding it disables editing.
- With **EDIT LAYER** off, a layer key toggles visibility and its audio. With editing on, click opens/closes the layer drawer; drag between rows changes the stacking order. Alt+Up/Down provides keyboard reordering.
- **ADD LAYER** adds the built-in test page at the top, initially hidden. New installations start with one visible test layer.
- **RELOAD** reloads all layers. The layer drawer has an individual Reload and confirmed Delete.
- Names/URLs apply on field blur. A changed valid URL automatically reloads that layer. Visibility applies immediately. Under Audio, each layer has mute and gain; the stereo sum is limited to prevent integer overflow.
- **SETUP** configures device, exact SDI mode, pixel format, Fill/Key, browser size and master audio mute. Stop PLAY before changing output settings. Custom dimensions appear below Browser Size.
- **IMAGE / KEY** choose the local monitor view. A preview is not proof of physical Fill/Key.
- **POWER** and Command+Q request confirmation before stopping output and quitting.

A valid saved device selection takes priority; otherwise the first detected device is selected and saved. Without compatible hardware, SETUP displays driver installation/authorization guidance while web rendering and local preview remain available. Device refresh is manual and does not change system permissions.

## Configuration and data

`~/Library/Application Support/URL2KeyFill/session.json` stores preferences and layer order, names, URLs, visibility, mute and gain. Writes are atomic. The previous `settings.txt` is imported when no session file exists. Invalid JSON is backed up before recovery. Deleting every layer preserves an empty stack on the next launch.

CEF uses a separate temporary incognito profile and `use-mock-keychain` on macOS. No personal browser profile, keychain password, microphone or screen capture is needed. Web authentication/cookies are not retained between application runs. No native bridge is exposed to URL content.

## Build and verify

See [BUILD](docs/BUILD.md), [architecture and audio timing](docs/ARCHITECTURE.md), [hardware connections](docs/HARDWARE.md), [validation evidence](docs/VALIDATION.md) and [remaining work](docs/BACKLOG.md).

The interface uses bundled HTML/CSS in a restricted native WKWebView; the video pages use CEF, not Safari or installed Google Chrome. Native code owns rendering buffers, device output and configuration.

## Development limits

Progressive HD/UHD modes in BGRA8/ARGB8; no interlaced field rendering, audio input capture, arbitrary native access, remote control or playlists. Up to 16 layers are accepted by this development build; this is a resource guard, not a performance guarantee. UHD60, large stacks, external key compositing, SDI audio listening and precise A/V alignment require validation with the receiving hardware. The Windows host remains future work. Local ad-hoc signing is not notarized distribution. **Not certified for production.**

## Built-in HTML graphics

In EDIT LAYER, the URL list uses the same appearance as the other settings lists. Choose a bundled graphic or select **Custom URL…** to enter an HTTP/HTTPS address.

The application scans its bundled `assets` directory at startup. Each subfolder forms a group; each HTML file appears by filename without the `.html` extension, with underscores replaced by spaces in both group and file names. The root `interface.html` is not a graphic example. Add new examples under `assets/<group>/<filename>.html` and rebuild the application to include them; no source-code list needs editing.

- `assets/Transparency/Basic_Test.html`: original transparency and motion test with an embedded stereo sound loop; the left channel retains the moving tone synchronized with the red rectangle; the right channel plays one click per second, alternating between two levels 12 dB apart. The complete loop is eight seconds.
- `assets/Lower_Third/Animated_10_sec_cycle.html`: a multi-part lower third on a transparent background. It starts entering at 3 seconds, starts exiting at 8 seconds, and repeats on a 10-second timeline (next entry at 13 seconds). RELOAD restarts the timeline.

Example URLs use `u2k://<group>/<filename>.html`. Earlier `u2k://assets/<group>/<filename>.html`, `u2k://test/index.html` and `u2k://lowerthird/index.html` URLs remain supported for saved sessions.

Each layer button includes a compact stereo LED meter measured from real captured source audio, before mute and gain. Muted, hidden or master-muted layers retain meter movement in grey. Meters continue to work while SDI is stopped; silence produces no lit segments.

The layer audio drawer includes the same pre-mute stereo meter. MUTE uses a red LED; the gain fader has marked ticks, resets to 0 dB on double-click, and accepts direct dB entry.

The master audio mute and stereo composite meter sit below FILL/KEY beside the monitor. The composite meter sums the actual aligned layer samples with their gains and per-layer mute, before master mute. It remains visible in grey when master mute or PAUSE is active. Double-click a layer's mini-meter (or press Alt+M with its button focused) to toggle that layer's mute without changing visibility.

Each layer's MONO button averages L and R and duplicates that signal to both output channels, avoiding a level increase from summing. This setting is saved per layer. Double-click the composite meter (or use Enter/Space when focused) to toggle master mute.

Meters measure per-channel RMS over a 300 ms window and display a common sine-calibrated dBFS scale: a full-scale sine reads 0 dBFS. Sixteen segments share the same 3 dB thresholds from −45 to −3 dBFS, with a final marker at −0.5 dBFS. The green working region ends at −18 dBFS; amber is above that reference. The visual maximum marker is driven only by levels the filled bar has actually reached, holds for 700 ms and falls at 12 dB/s. Raw sample peaks remain available in the tooltip. Captured meter history is kept separately from playback queues, so scheduled playback cannot remove samples needed by the display. Hovering shows the sine-calibrated RMS level and raw sample peaks in dBFS. Source meters are before gain/mute; the composite meter includes layer gain, mute and mono processing.

MONO layers show a single bar in both their layer button and audio drawer, measured after the mono fold-down and before gain/mute.

Audio diagnostics distinguish **Buffer overruns** (packets expiring while needed for active, unmuted SDI output) from **Unused expiry (normal)** (normal queue cleanup while stopped, paused, muted, hidden or at zero gain). Expiry of packets captured before PLAY remains normal after starting PLAY. Counters accumulate for each layer runtime; SDK/output failures remain in **Errors**.
