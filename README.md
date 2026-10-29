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
