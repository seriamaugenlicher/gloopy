#!/usr/bin/env bash
# Package the built cores into one .zip per platform for a GitHub release.
#
# Reads the per-platform binaries produced by scripts/build-release.sh (desktop +
# Android) plus the macOS dylibs, which come from CI and must be dropped into dist/
# under the names below first. Each zip is a self-contained manual-install drop: the
# core under its canonical libretro filename, plus the info file, README, and the
# licence/notices. Also writes a .sha256 next to each zip.
#
# Run scripts/build-release.sh first (and stage the macOS dylibs). Missing binaries
# are skipped with a warning, so a partial set still packages what is present.
set -euo pipefail

cd "$(dirname "$0")/.."
DIST="$(pwd)/dist"
VER="$(sed -n 's/^display_version *= *"\(.*\)"/\1/p' gloopy_libretro.info)"
: "${VER:?could not read display_version from gloopy_libretro.info}"

# platform-key | source binary in dist/ | canonical name inside the zip
ROWS="
windows-x86_64 | gloopy_libretro.dll                | gloopy_libretro.dll
linux-x86_64   | gloopy_libretro_linux_x86_64.so    | gloopy_libretro.so
linux-aarch64  | gloopy_libretro_linux_aarch64.so   | gloopy_libretro.so
macos-x86_64   | gloopy_libretro_macos_x86_64.dylib | gloopy_libretro.dylib
macos-arm64    | gloopy_libretro_macos_arm64.dylib  | gloopy_libretro.dylib
android-arm64  | gloopy_libretro_android_arm64.so   | gloopy_libretro_android.so
android-armv7  | gloopy_libretro_android_arm.so     | gloopy_libretro_android.so
"

sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"; else shasum -a 256 "$1"; fi; }

# Zip the *contents* of a directory into $2, using whatever tool is available.
make_zip() {
	local stage="$1" out="$2"
	rm -f "$out"
	if command -v zip >/dev/null 2>&1; then
		( cd "$stage" && zip -q -X -r "$out" . )
	elif command -v powershell.exe >/dev/null 2>&1; then
		powershell.exe -NoProfile -NonInteractive -Command \
			"Compress-Archive -Path '$(cygpath -w "$stage")\\*' -DestinationPath '$(cygpath -w "$out")' -Force" >/dev/null
	else
		echo "error: need 'zip' or PowerShell to create archives" >&2
		return 1
	fi
}

made=0 skipped=0
while IFS='|' read -r key src canon; do
	key="$(echo "$key" | xargs)"; src="$(echo "$src" | xargs)"; canon="$(echo "$canon" | xargs)"
	[ -z "$key" ] && continue
	if [ ! -f "$DIST/$src" ]; then
		echo "skip $key: $src not in dist/ (build it, or stage the macOS dylib)"
		skipped=$((skipped+1)); continue
	fi
	stage="$(mktemp -d)"
	cp "$DIST/$src" "$stage/$canon"
	cp gloopy_libretro.info README.md LICENSE NOTICES.md "$stage/"
	out="$DIST/gloopy_libretro-$key.zip"
	make_zip "$stage" "$out"
	rm -rf "$stage"
	# checksum records the bare filename, so the sidecar is portable
	( cd "$DIST" && sha "$(basename "$out")" > "$(basename "$out").sha256" )
	echo "made  $(basename "$out")"
	made=$((made+1))
done <<EOF
$ROWS
EOF

echo
echo "packaged $made zip(s), skipped $skipped, version $VER, in dist/"
