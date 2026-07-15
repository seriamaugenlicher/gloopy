#!/usr/bin/env bash
# Fast iteration loop: rebuild the Windows core and smoke-test 4 representative
# ROMs. Seconds, not minutes - the full sweep (every platform, all 13 ROMs) is
# scripts/build-release.sh plus a full harness run, and is for release only.
#
# Needs LOOPY_ROMS and LOOPY_SYS in scripts/local-env.sh (gitignored). No ROM or
# BIOS is distributed with this repository; supply your own.
#
# The four titles are chosen for coverage, not convenience:
#   Loopy Town    - the standard case, and the scene the perf work was measured on
#   Magical Shop  - its hot loop is a REAL memset that must NOT be idle-skipped
#   Dream Change  - has its own idle loop in game ROM rather than the BIOS one
#   Wanwan        - exercises the expansion PCM hardware path
#
#   ./scripts/smoke.sh                 rebuild + test + install into RetroArch
#   ./scripts/smoke.sh --no-build      test the existing DLL
#   ./scripts/smoke.sh --no-deploy     do not touch the RetroArch install
#   ./scripts/smoke.sh key=value ...   pass core options through to the harness
#
# A passing build is installed into $RETROARCH_DIR automatically, so the core
# RetroArch loads is never out of date with the source. Skipped if the tests fail:
# a broken core should not replace a working one.

set -uo pipefail
cd "$(dirname "$0")/.."
ROOT=$(pwd)

[ -f scripts/local-env.sh ] && . scripts/local-env.sh
MAKE_BIN=${MAKE_BIN:-make}
[ -n "${MINGW_BIN:-}" ] && PATH="$MINGW_BIN:$PATH"

for v in LOOPY_ROMS LOOPY_SYS; do
	[ -n "${!v:-}" ] || { echo "error: \$$v is not set (see scripts/local-env.sh)"; exit 1; }
done

ROMS=(
	"Loopy Town no Oheya ga Hoshii"
	"Magical Shop"
	"Dream Change Kokinchan no Fashion Party"
	"Wanwan Aijou Monogatari"
)

BUILD=1
DEPLOY=1
OPTS=()
for a in "$@"; do
	case "$a" in
		--no-build) BUILD=0 ;;
		--no-deploy) DEPLOY=0 ;;
		*=*) OPTS+=("$a") ;;
		*) echo "unknown argument: $a"; exit 1 ;;
	esac
done

if [ "$BUILD" -eq 1 ]; then
	"$MAKE_BIN" -j"${JOBS:-8}" platform=win 2>&1 | grep -E "error|Error" && { echo "BUILD FAILED"; exit 1; }
	echo "built dist/gloopy_libretro.dll"
fi

# The harness loads the core with LoadLibrary, so it is Windows-only
[ -x tools/harness/harness.exe ] || \
	(cd tools/harness && "$MAKE_BIN" CXX=g++ >/dev/null 2>&1) || \
	{ echo "could not build the harness"; exit 1; }

OUT="$ROOT/out/smoke"
mkdir -p "$OUT"

CORE=$(cygpath -w "$ROOT/dist/gloopy_libretro.dll")
SYS=$(cygpath -w "$LOOPY_SYS")
HARNESS="$ROOT/tools/harness/harness.exe"

pass=0; fail=0
printf "\n%-42s %-10s %-10s %s\n" "ROM" "WRAM" "AUDIO" "RESULT"
printf -- "------------------------------------------------------------------------\n"
for name in "${ROMS[@]}"; do
	rom="$LOOPY_ROMS/$name.bin"
	[ -f "$rom" ] || { printf "%-42s %s\n" "$name" "NOT FOUND"; fail=$((fail+1)); continue; }

	# cygpath: git bash mangles paths with apostrophes/brackets on the way to a native exe
	romw=$(cygpath -w "$rom")
	outw=$(cygpath -w "$OUT")

	res=$("$HARNESS" "$CORE" "$romw" "$SYS" "$outw" "${OPTS[@]}" 2>&1)
	wram=$(echo "$res" | grep -oE "wram_crc: [0-9A-F]+" | awk '{print $2}')
	audio=$(echo "$res" | grep -oE "audio_crc: [0-9A-F]+" | awk '{print $2}')

	if echo "$res" | grep -q "FAIL"; then
		printf "%-42s %-10s %-10s %s\n" "$name" "${wram:--}" "${audio:--}" "FAIL"
		echo "$res" | grep "FAIL" | sed 's/^/    /'
		fail=$((fail+1))
	else
		printf "%-42s %-10s %-10s %s\n" "$name" "$wram" "$audio" "pass"
		pass=$((pass+1))
	fi
done

printf -- "------------------------------------------------------------------------\n"
echo "pass: $pass   fail: $fail"

# The frontend keeps its own copy of the core, so a rebuild here changes nothing
# until it is installed there - which is an easy way to spend an afternoon testing
# the build you did not make. Install on every pass so the two cannot drift.
if [ "$DEPLOY" -eq 1 ] && [ -n "${RETROARCH_DIR:-}" ]; then
	if [ "$fail" -ne 0 ]; then
		echo "not deploying: tests failed, leaving the installed core alone"
	elif cp dist/gloopy_libretro.dll "$RETROARCH_DIR/cores/" &&
	     cp gloopy_libretro.info "$RETROARCH_DIR/info/"; then
		echo "deployed to $RETROARCH_DIR"
	else
		echo "WARNING: could not deploy to $RETROARCH_DIR (is RetroArch running?)"
	fi
fi
# The CRCs above fingerprint work RAM and the audio stream. Compare them across
# builds to prove a change did not alter emulation - a pixel check cannot show
# that, since e.g. a skipped frame legitimately differs.
[ "$fail" -eq 0 ]
