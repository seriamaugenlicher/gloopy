#!/usr/bin/env sh
# Produce a self-contained "complete corresponding source" bundle for release.
#
# The bundle is the GPL source drop: anyone can unpack it, host it in a repository
# of their own, build it, and submit it to libretro without any dependence on the
# original repo staying up. See HANDOFF.md.
#
# It is built with `git archive`, so it contains exactly the tracked files and
# NOTHING gitignored — the internal working notes (CLAUDE.md, claudedocs/,
# scripts/local-env.sh) and all build output are excluded by construction.
#
# Usage: scripts/make-release-bundle.sh [ref]     (ref defaults to HEAD)
set -eu

VER="$(sed -n 's/^display_version *= *"\(.*\)"/\1/p' gloopy_libretro.info)"
: "${VER:?could not read display_version from gloopy_libretro.info}"
REF="${1:-HEAD}"
OUT="dist/gloopy-${VER}-src.tar.gz"

mkdir -p dist
git archive --format=tar.gz --prefix="gloopy-${VER}/" "${REF}" -o "${OUT}"

# Checksum alongside the bundle.
if command -v sha256sum >/dev/null 2>&1; then
	sha256sum "${OUT}" | tee "${OUT}.sha256"
else
	shasum -a 256 "${OUT}" | tee "${OUT}.sha256"
fi

echo "bundle:  ${OUT}"
echo "files:   $(git archive "${REF}" | tar -t | grep -vc '/$')"
