# Architecture — Studio Rack 0.2

## Native host and trusted interface

`src/platform/mac/Main.mm` owns the AppKit window, a non-persistent WKWebView for the bundled Studio Rack interface, the native image monitor, persistence and device commands. `assets/interface.html` is a local application resource, with no external scripts or network access. The native message handler accepts only the main frame of this bundled file. Navigation away from the interface is blocked; intentional Blackmagic support links open in the system browser.

The interface's bridge is not present in the browsers that render user URLs. Those are separate CEF off-screen instances with Chromium sandbox, TLS and origin protections active. No personal Chromium profile, screen capture, microphone, shell API or keychain secret is used. Each run has an exclusive temporary profile; the macOS mock-keychain backend prevents Chromium Safe Storage prompts. Temporary profiles are removed on clean exit.

The window has no title bar. Dragging unused background reports a drag intent to AppKit, which moves the window using pointer positions while the left mouse button is held. Interactive controls are excluded. The monitor is a native NSView above the WKWebView, positioned from the actual local monitor rectangle; no PNG/JPEG encoding is used in the rendering or SDI pipeline. The monitor is hidden while confirmation dialogs are open, so it cannot cover them.

AppKit and WebKit are system frameworks, with no third-party UI toolkit or redistributable browser dependency for the interface. CEF remains BSD/Chromium with bundled notices; the DeckLink SDK has its own EULA. The Windows host is not implemented; the composition/audio core, CEF client and interface asset can be reused with a Windows native host and a restricted interface bridge.

## Independent layers and composition

Each layer owns a CEF browser, an immutable latest BGRA frame and a bounded audio lane. CEF renders with transparent background and device scale 1; CSS opaque backgrounds remain opaque. `OnPaint` copies the frame before returning. Browser errors clear that layer's image and audio; other layers continue.

The scene worker examines latest frames about every 16 ms and only recomposes when frame pointers or scene configuration change. It blends from the bottom of the ordered stack to the top, using premultiplied source-over for all four channels. Scene configuration and source ownership are protected separately from the UI. Deleting a layer closes its browser asynchronously; retired runtimes remain owned until `OnBeforeClose`.

All layer browsers use the selected browser viewport. The composed viewport is fitted proportionally into the SDI target with transparent margins. Bilinear interpolation works on premultiplied channels. The native local monitor draws its checkerboard separately; that checkerboard never enters SDI. KEY shows the alpha mask locally. A static scene retains its last frame while scheduled SDI repeats it.

The development host permits at most 16 layers, independently of screen size. This is a resource guard, not a measured performance claim. The integration test uses two Full HD browser instances. Large stacks and UHD60 need measurement and optimization.

## SDI video and alpha

The backend enumerates real output devices, active-profile modes and both BGRA/ARGB support. Ordinary output and Keying support are queried independently: the HD Mini can accept RGB at 1080p50/59.94/60 for Keying even when the ordinary-RGB query rejects it. Exact rational frame durations are retained. Output is revalidated immediately before starting.

Three owned SDK frames are scheduled with `ScheduleVideoFrame`. Video callbacks return reusable slots to a bounded queue and count completions, late and dropped frames. A worker does conversion and schedules the next frame; it skips past elapsed stream time after a serious delay instead of growing latency.

CEF BGRA is premultiplied sRGB8. At the SDK boundary, RGB is unpremultiplied, decoded from sRGB and encoded with the SDR Rec.709 transfer function using a lookup table; alpha remains full-range coverage. Primaries coincide. SDK frame metadata declares Rec.709. SDI YCbCr conversion and physical levels are delegated to Desktop Video; waveform confirmation remains necessary. An 8-bit web source does not acquire real 10-bit precision from the SDI transport.

RGB8 is supported for progressive HD/UHD; interlaced/SD and exclusively YUV output devices are not implemented. The physical straight/premultiplied behavior after the device keyer must still be checked against the receiving switcher, using the test patches.

## Web audio and SDI embedding

`CefAudioHandler` requests planar float stereo at 48 kHz, 480 frames per packet. It copies packets into owned, per-layer queues with a maximum of 64 packets and approximately 500 ms of timestamp history. Old data is discarded. Missing audio produces silence. NaN/Inf samples are sanitized.

**CEF timestamp compatibility:** the pinned build delivered monotonic millisecond timestamps in the measured callback, despite the header describing Unix-epoch milliseconds. At stream start the client detects this clock-domain difference and records a constant offset to the system timeline. Packet intervals are preserved; the offset is reset when a new stream starts. This correction was required to get non-zero audio into the scheduled SDI mixture.

The mixer sums visible, unmuted layers, applying gain 0–2, then hard-limits the stereo result to signed 32-bit PCM. Master mute is available in Setup Advanced; layer mute/gain are in the layer drawer's Audio section. Hiding a layer mutes its audio. PAUSE mutes the full audio mix and makes video transparent, without stopping SDI. PLAY stopped releases audio and video output. No microphone or system-loopback capture is performed.

DeckLink audio uses 48 kHz, 32-bit integer stereo, timestamped scheduling and an audio callback. A bounded preroll target is derived from three video frames (clamped to 30–250 ms); the callback fills that budget in blocks of at most 1024 samples and resynchronizes to the scheduled stream clock after an underrun. The audio callback owns a reference to the output interface; it is stopped and detached before release. Fatal audio API failures cause controlled output shutdown. Diagnostics distinguish captured frames, scheduled frames, clock recoveries/API errors, audible mixed samples and limited samples.

This establishes a shared scheduling timeline, but does not constitute external A/V sync measurement. Browser/OS/device latency and the receiver's processing require loopback/listening tests. Pause, visibility and mute changes take effect after already scheduled buffers drain.

## Persistence, first run and failure handling

The versioned `session.json` stores the stack, per-layer controls and output preferences atomically. Invalid JSON is backed up before recovery. Legacy `settings.txt` is imported on the first new-format run. Only a genuinely new configuration creates one visible test layer; a saved empty stack remains empty.

The first detected compatible device is selected and saved unless a valid stored/user selection remains in the detected list. No compatible device opens SETUP with driver installation/authorization guidance. Refresh does not change driver permissions or device profiles. Configuration changes stop short of altering external Blackmagic profile or driver settings.

Device/format configuration is locked during PLAY. Device loss is detected by SDK failure or two seconds without video completions, followed by controlled stop. Reconnection is manual through Refresh. Quit requires confirmation only while SDI output is active (including PAUSE), then waits for all CEF browser close notifications before terminating the message loop and calling `CefShutdown`.
