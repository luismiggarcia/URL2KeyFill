#!/bin/bash
set -euo pipefail
# Run after building. The caller is responsible for closing the previous app.
root="$(cd "$(dirname "$0")/.." && pwd)"
destination="${1:-$HOME/Applications/URL2KeyFill.app}"
parent="$(dirname "$destination")"
mkdir -p "$parent"
staging="$(mktemp -d "$parent/.url2keyfill-install.XXXXXX")"
trap 'rmdir "$staging" 2>/dev/null || true' EXIT
/usr/bin/ditto --noextattr "$root/build/URL2KeyFill.app" "$staging/URL2KeyFill.app"
/bin/bash "$root/scripts/sign-local.sh" "$staging/URL2KeyFill.app"
/usr/bin/codesign --verify --deep --strict "$staging/URL2KeyFill.app"
if [ -e "$destination" ]; then
 backup="$parent/.URL2KeyFill.previous.$(date +%Y%m%d%H%M%S).app"
 mv "$destination" "$backup"
fi
mv "$staging/URL2KeyFill.app" "$destination"
echo "$destination"
