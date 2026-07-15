#!/usr/bin/env bash
# Build the release core for every platform into dist/.
#
# Toolchain locations come from the environment, so that no machine's paths are
# baked into this repository. Put yours in scripts/local-env.sh (gitignored):
#
#   MINGW_BIN=/c/msys64/mingw64/bin
#   ANDROID_NDK=/e/tools/android-ndk-r27c
#   ZIG=/e/tools/zig/zig.exe
#   MAKE_BIN=mingw32-make        # MSYS2 ships no plain 'make' in mingw64
#
# Build only some targets by naming them:  ./scripts/build-release.sh linux-aarch64
# Targets: windows linux-aarch64 linux-x86_64 android-arm64 android-arm
#
# Why zig for Linux but clang for Android: the NDK's clang links only against
# bionic, so it cannot produce a linux-gnu core at all. Prefer clang wherever it
# can reach the target; zig covers the rest.

set -euo pipefail

cd "$(dirname "$0")/.."
DIST="$(pwd)/dist"

[ -f scripts/local-env.sh ] && . scripts/local-env.sh

JOBS=${JOBS:-8}
MAKE_BIN=${MAKE_BIN:-make}

# make itself lives here on Windows, so every target needs it on PATH - not just
# the Windows one. The cross builds name their compilers explicitly (zig via
# CC/CXX, clang via the NDK), so nothing here can shadow them.
[ -n "${MINGW_BIN:-}" ] && PATH="$MINGW_BIN:$PATH"
# zig spells the cpu with an underscore, and needs UBSan turned off explicitly:
# 'zig cc' enables trap-mode UBSan by default even at -O2, which aborts the core
# at runtime on an unimplemented-hardware path and cost ~40% speed on an A53.
ARM_ZIG="-O3 -flto -mcpu=cortex_a53 -DNDEBUG -fno-sanitize=undefined"
X64_ZIG="-O3 -flto -DNDEBUG -fno-sanitize=undefined"
ARM_CLANG="-O3 -flto -mcpu=cortex-a53 -DNDEBUG"

mkdir -p "$DIST"

TARGETS=("$@")
want() {
	[ ${#TARGETS[@]} -eq 0 ] && return 0
	local t
	for t in "${TARGETS[@]}"; do [ "$t" = "$1" ] && return 0; done
	return 1
}
have() { [ -n "${!1:-}" ] || { echo "skip $2: \$$1 is not set"; return 1; }; }
build() { echo; echo "=== $1 ==="; shift; "$MAKE_BIN" -j"$JOBS" "$@"; }

if want windows && have MINGW_BIN windows; then
	build "windows x86_64" platform=win
fi

if want linux-aarch64 && have ZIG linux-aarch64; then
	build "linux aarch64" platform=unix BUILD_TAG=aarch64 \
		CC="$ZIG cc -target aarch64-linux-gnu" \
		CXX="$ZIG c++ -target aarch64-linux-gnu" \
		OPTIMIZE="$ARM_ZIG" OPTIMIZE_LD="-O3 -flto -mcpu=cortex_a53"
	"$ZIG" objcopy --strip-all "$DIST/gloopy_libretro.so" "$DIST/gloopy_libretro_linux_aarch64.so"
	rm -f "$DIST/gloopy_libretro.so"
fi

if want linux-x86_64 && have ZIG linux-x86_64; then
	build "linux x86_64" platform=unix BUILD_TAG=x86_64 \
		CC="$ZIG cc -target x86_64-linux-gnu" \
		CXX="$ZIG c++ -target x86_64-linux-gnu" \
		OPTIMIZE="$X64_ZIG" OPTIMIZE_LD="-O3 -flto"
	"$ZIG" objcopy --strip-all "$DIST/gloopy_libretro.so" "$DIST/gloopy_libretro_linux_x86_64.so"
	rm -f "$DIST/gloopy_libretro.so"
fi

if want android-arm64 && have ANDROID_NDK android-arm64; then
	build "android arm64" platform=android_arm64 ANDROID_NDK="$ANDROID_NDK" \
		OPTIMIZE="$ARM_CLANG" OPTIMIZE_LD="-O3 -flto -mcpu=cortex-a53"
	mv "$DIST/gloopy_libretro_android.so" "$DIST/gloopy_libretro_android_arm64.so"
fi

if want android-arm && have ANDROID_NDK android-arm; then
	build "android armv7" platform=android_arm ANDROID_NDK="$ANDROID_NDK" \
		OPTIMIZE="-O3 -flto -DNDEBUG" OPTIMIZE_LD="-O3 -flto"
	mv "$DIST/gloopy_libretro_android.so" "$DIST/gloopy_libretro_android_arm.so"
fi

cp gloopy_libretro.info "$DIST/"

echo
echo "=== dist ==="
ls -l "$DIST"
