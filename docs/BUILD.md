# Build, install and run

## Pinned dependencies

- C++20 and CMake **3.31.6** (minimum 3.21).
- CEF **156.0.14+g4fa4ab5+chromium-156.0.8078.48**, `macosarm64_minimal` distribution dated 2026-10-24. Chromium is embedded; installed Google Chrome is not used.
- Blackmagic **Desktop Video SDK 16.0**, obtained separately from the official developer portal. CMake verifies the headers' version.
- macOS Apple Silicon, including M1. CEF's deployment target is macOS 13; the Desktop Video driver may require a newer system. See VALIDATION.md for the environment actually tested.
- Installed Desktop Video runtime used for development: **16.4**. SDK and runtime versions are different.

## Dependency setup

1. Install Command Line Tools (`xcode-select --install`) or Xcode. CEF recommends Xcode 16+; this project was built with AppleClang 21 from Command Line Tools.
2. Install [CMake 3.31.6](https://github.com/Kitware/CMake/releases/tag/v3.31.6), making `cmake` and `ctest` available on PATH.
3. Download the [pinned Apple Silicon CEF distribution](https://cef-builds.spotifycdn.com/cef_binary_156.0.14%2Bg4fa4ab5%2Bchromium-156.0.8078.48_macosarm64_minimal.tar.bz2). Extract it into `third_party/`, retaining the directory name `cef_binary_156.0.14+g4fa4ab5+chromium-156.0.8078.48_macosarm64_minimal`.
4. Obtain the [Desktop Video 16.0 SDK](https://www.blackmagicdesign.com/developer/products/capture-and-playback/sdk-and-software), completing the vendor's registration and license requirements. Extract it into `third_party/DeckLinkSDK`. The file `third_party/DeckLinkSDK/Mac/include/DeckLinkAPI.h` must exist. Installing the driver alone does not supply these headers.
5. Install Desktop Video and authorize its Blackmagic extensions as requested. Restart if required by the installer. Connect UltraStudio using a compatible **Thunderbolt** cable, not merely a USB-C cable.

SHA-256 of the original dependency archives used:

```text
CEF minimal arm64:
ed994b907ad596ca609a0542a1e2b22aa97e93e98b8141fed4c24aef39b9ccf6
CMake 3.31.6 macos-universal:
330b9514f5112e5ed4fb08b8b05803b776fd9b539a6ae12927d14dcc0ee2ba8d
```

Third-party binaries and SDK files are excluded from Git. Preserve the CEF/Chromium notices and Blackmagic EULA. SDK headers are not redistributed inside the application bundle.

## Full Apple Silicon build

From the project root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DU2K_BUILD_APP=ON -DU2K_DECKLINK=ON
cmake --build build --parallel 6
ctest --test-dir build --output-on-failure
open build/URL2KeyFill.app
```

The bundle includes the CEF framework, resources, license notices and sandboxed Helper processes. The trusted interface is bundled HTML/CSS hosted by the system WebKit framework; no npm dependencies, external scripts or preview server are needed. CEF renders all video URLs.

Use `-DCEF_ROOT=/path/to/cef` and `-DDECKLINK_SDK_ROOT=/path/to/SDK` to override dependency locations. The supplied build is ad-hoc signed, not Developer ID signed or notarized. Do not disable Chromium sandboxing, TLS or origin checks to resolve startup problems.

## Core-only build

```sh
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Release
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

`-DU2K_BUILD_APP=ON -DU2K_DECKLINK=OFF` builds CEF and the interface while explicitly reporting the missing hardware backend. No output devices are simulated. Published application bundles are built with `U2K_DECKLINK=ON`.

## Diagnostics and integration tests

```sh
./build/decklink_probe
./build/URL2KeyFill.app/Contents/MacOS/URL2KeyFill --smoke-test
./build/URL2KeyFill.app/Contents/MacOS/URL2KeyFill --smoke-no-device-test
./build/URL2KeyFill.app/Contents/MacOS/URL2KeyFill --hardware-smoke-test
```

The probe enumerates real devices and supported modes without starting output. The normal integration test uses temporary settings, two transparent local CEF pages and a generated 440 Hz tone captured internally. It checks the interface bridge, composition, persistence, audio capture and clean shutdown without starting SDI.

The no-device test deliberately supplies an empty device list to exercise driver guidance and disabled PLAY. Real CEF rendering/audio capture continue; fake successful hardware playback is never supplied.

The hardware test deliberately starts SDI video and stereo audio on a detected device and exercises pause, resume and stop. Use it only when interrupting the connected output is acceptable. All integration tests close automatically. They require normal desktop/driver access but no screen recording or microphone permission.

## Settings and local installation

Current settings are stored atomically in `~/Library/Application Support/URL2KeyFill/session.json`; the older `settings.txt` is imported if no session exists. Invalid JSON is backed up before recovery. See README.md for persisted fields.

CEF uses an exclusive temporary incognito profile and the macOS `use-mock-keychain` backend. No personal Chrome profile or keychain password is required. Web cookies/authentication are not retained between runs. The legacy `CEF` profile directory is no longer used or modified.

Close the installed application before updating, then run:

```sh
bash scripts/install-local.sh
open "$HOME/Applications/URL2KeyFill.app"
```

The installer stages, signs and verifies the new bundle before replacing the installed copy. It preserves the previous version as a hidden timestamped `.URL2KeyFill.previous.*.app` in the same Applications directory. Existing Desktop aliases and symlinks retain their target. Install outside OneDrive to avoid Finder metadata being reintroduced into signed bundles by synchronization.

## Distribution and Windows

Public macOS distribution still needs Developer ID signing, suitable CEF entitlements, notarization and clean-machine validation. Ad-hoc signing is appropriate for these development releases, not a claim of Apple notarization.

The core, layer composition, audio mixing, URL policy, image processing and CEF client are portable C++. Windows still needs a native host, trusted local interface bridge, native preview, CEF/sandbox bootstrap, COM initialization, DeckLink discovery and string/GUID adaptation, settings paths and atomic replacement. The current DeckLink implementation includes macOS-specific adaptation. A matching Windows CEF distribution, runtime packaging, signing and installer must also be provided. No Windows executable is currently supplied.
