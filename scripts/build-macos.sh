#!/usr/bin/env bash
# Build the macOS core natively. Run this ON a Mac, from the repo root:
#
#   ./scripts/build-macos.sh
#
# Needs the Xcode command line tools:  xcode-select --install
#
# Produces, in dist/:
#   gloopy_libretro_macos_arm64.dylib
#   gloopy_libretro_macos_x86_64.dylib
#   gloopy_libretro.dylib              <- universal (both arches), the one to ship
#
# Both arches are cross-buildable from either kind of Mac: the Xcode SDK carries
# the stubs for both, so an Apple Silicon machine can emit x86_64 and vice versa.
# Only the host arch can actually be RUN without Rosetta, though.

set -euo pipefail

cd "$(dirname "$0")/.."
DIST="$(pwd)/dist"
mkdir -p "$DIST"

JOBS=${JOBS:-$(sysctl -n hw.ncpu)}

for arch in arm64 x86_64; do
	echo
	echo "=== macos $arch ==="
	# Apple Silicon did not exist before 11.0, so the Makefile's 10.15 default is
	# not a legal deployment target for arm64.
	[ "$arch" = arm64 ] && minver=11.0 || minver=10.15
	# -arch has to reach the compile AND the link, so it goes in both.
	make -j"$JOBS" platform=osx BUILD_TAG="$arch" \
		MINVERSION="-mmacosx-version-min=$minver" \
		OPTIMIZE="-O3 -flto -DNDEBUG -arch $arch" \
		OPTIMIZE_LD="-O3 -flto -arch $arch"
	mv gloopy_libretro.dylib "$DIST/gloopy_libretro_macos_$arch.dylib"
done

echo
echo "=== universal ==="
lipo -create \
	"$DIST/gloopy_libretro_macos_arm64.dylib" \
	"$DIST/gloopy_libretro_macos_x86_64.dylib" \
	-output "$DIST/gloopy_libretro.dylib"
lipo -info "$DIST/gloopy_libretro.dylib"

cp gloopy_libretro.info "$DIST/"

echo
echo "=== dist ==="
ls -l "$DIST"/*.dylib

cat <<'NOTE'

To test in RetroArch on this Mac:
  cp dist/gloopy_libretro.dylib  ~/Library/Application\ Support/RetroArch/cores/
  cp dist/gloopy_libretro.info   ~/Library/Application\ Support/RetroArch/info/
BIOS goes in RetroArch's system directory: loopy_bios.bin (required),
loopy_soundbios.bin (optional, needed for sound).

Gatekeeper will not block a core loaded by RetroArch, but if macOS ever quarantines
it:  xattr -dr com.apple.quarantine dist/gloopy_libretro.dylib
NOTE
