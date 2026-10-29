#!/bin/bash
set -eu
app="$1"
# OneDrive/Finder can add these attributes to bundle directories; remove only
# packaging metadata, never quarantine or security provenance attributes.
/usr/bin/xattr -dr com.apple.FinderInfo "$app" 2>/dev/null || true
/usr/bin/xattr -dr com.apple.ResourceFork "$app" 2>/dev/null || true
/usr/bin/codesign --force --sign - "$app/Contents/Frameworks/Chromium Embedded Framework.framework"
# Development-only ad-hoc signing. Distribution requires Developer ID + notarization.
while IFS= read -r -d '' helper; do
 /usr/bin/codesign --force --sign - "$helper"
done < <(find "$app/Contents/Frameworks" -maxdepth 1 -name '*Helper*.app' -print0)
/usr/bin/codesign --force --sign - "$app"

# Signing inside OneDrive may add Finder metadata again; it is not signed content.
/usr/bin/xattr -dr com.apple.FinderInfo "$app" 2>/dev/null || true
/usr/bin/xattr -dr com.apple.ResourceFork "$app" 2>/dev/null || true
