#!/bin/bash
# Build an instrumented copy of URL2KeyFill.app that logs every CEF audio packet.
# Reuses the CEF wrapper and DeckLink objects of an existing configured build tree;
# the tracked sources and the original bundle are not modified.
#
# Usage: build-instrumented.sh <work directory> [build directory]
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$1"; build="${2:-$root/build}"
cef="$(find "$root/third_party" -maxdepth 1 -name 'cef_binary_*' -type d | head -1)"
sdk="$(xcrun --show-sdk-path)"
for f in "$build/URL2KeyFill.app" "$build/libcef_dll_wrapper/libcef_dll_wrapper.a" \
         "$build/CMakeFiles/URL2KeyFill.dir/src/output/DeckLink.cpp.o"; do
 [[ -e "$f" ]] || { echo "Missing $f: build the application first (docs/BUILD.md)." >&2; exit 1; }
done
mkdir -p "$work"; rm -rf "$work/src" "$work/URL2KeyFill.app"
cp -R "$root/src" "$work/src"
python3 -I "$root/tools/audio-audit/instrument.py" "$work/src/browser/Browser.hpp"
common=(-O3 -DNDEBUG -arch arm64 -isysroot "$sdk" -mmacosx-version-min=13.0 -I"$cef" -I"$work/src")
cd "$work"
for f in core/Core core/Pipeline core/Scene core/MediaClock browser/Browser; do
 c++ "${common[@]}" -std=gnu++20 -c "src/$f.cpp" -o "$(basename "$f").o"
done
c++ "${common[@]}" -std=gnu++2a -fobjc-arc -I"$root/third_party/DeckLinkSDK/Mac/include" -c src/platform/mac/Main.mm -o Main.o
cp -cR "$build/URL2KeyFill.app" URL2KeyFill.app 2>/dev/null || cp -R "$build/URL2KeyFill.app" URL2KeyFill.app
c++ "${common[@]}" -Wl,-search_paths_first -Wl,-headerpad_max_install_names Main.o \
 "$build/CMakeFiles/URL2KeyFill.dir/src/output/DeckLink.cpp.o" \
 "$build/CMakeFiles/URL2KeyFill.dir/third_party/DeckLinkSDK/Mac/include/DeckLinkAPIDispatch.cpp.o" \
 Browser.o Core.o Pipeline.o Scene.o MediaClock.o "$build/libcef_dll_wrapper/libcef_dll_wrapper.a" \
 -lpthread -framework AppKit -framework Cocoa -framework IOSurface -framework CoreFoundation \
 -framework WebKit -framework QuartzCore -framework AVFoundation \
 -o URL2KeyFill.app/Contents/MacOS/URL2KeyFill
codesign --force --sign - URL2KeyFill.app
echo "Instrumented application: $work/URL2KeyFill.app"
