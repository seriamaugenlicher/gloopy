# Status, redistribution, and adoption

Gloopy 1.0.0 is feature-complete: video, audio, gamepad and Loopy Mouse (with hot-swap),
savestates and rewind, `.srm` battery saves, the seal printer, and the *Wanwan* expansion PCM
all work, and the full commercial library runs.

**It is provided as-is, without warranty of any kind, express or implied, and is not actively
maintained.** There is no support and no issue tracker (Issues and pull requests are disabled).
It works and is complete; it just isn't being developed further.

## License and what you may do

Gloopy is licensed **GPL v3** (see `LICENSE` and `NOTICES.md`). Under that license you are free
to fork it, rebuild it, redistribute it, submit it to libretro so it appears in RetroArch's
**Core Downloader**, and maintain your own version — no permission needed beyond GPL v3's terms.
No copyright is claimed over the modifications in this fork; upstream's GPL still applies.

## Attribution to keep

The emulator itself — the SH-2 interpreter, the VDP, the uPD937 sound synthesis, and the
reverse engineering behind them — is the work of the **LoopyMSE** authors, **PSI** and
**kasami** (<https://github.com/PSI-Rockin/LoopyMSE>). Gloopy is an independent hard fork that
adapts LoopyMSE to libretro and tunes it for low-power hardware.

## Getting it into the Core Downloader

Everything libretro's build system needs is already in this repository:

1. **Host the source publicly.** Fork this repo, or unpack the release source bundle
   (`gloopy-1.0.0-src.tar.gz`) into a repository of your own. libretro's build-bot builds from a
   public git repo, so it needs one to point at.

2. **The build-bot config is included.** `.gitlab-ci.yml` builds the desktop and Android targets
   via libretro's shared CI templates. It drives the build through `Makefile.libretro` (a thin
   wrapper that outputs the core to the repo root, where the build-bot collects it) and builds
   Android through `jni/Android.mk`. The platform list is limited to the toolchain families the
   C++17 core is known to build under (MinGW, glibc, the Android NDK, Apple clang); other targets
   can be added to `.gitlab-ci.yml` as they are made to build.

3. **Submit the core info file.** Open a pull request adding `gloopy_libretro.info` (included
   here, verbatim) to `libretro/libretro-super` at `dist/info/gloopy_libretro.info`. This is the
   file RetroArch reads to describe the core. The `Casio - Loopy` database already exists in
   `libretro/libretro-database`, so scanning and playlists work with no further database work.

4. **Request a build-bot mirror + crawl-list entry.** This is not a pull request: the build-bot
   only builds repositories mirrored onto libretro's GitLab instance and placed on its crawl
   list, which is arranged by contacting a libretro maintainer on the libretro Discord (the
   `#programming` channel). Point them at your public repository and its `.gitlab-ci.yml`.

5. **Optional: a documentation page.** libretro's docs live at `libretro/docs`; a core page can
   be adapted from this repository's `README.md`, following their `docs/meta/core-template.md`.

Once the build-bot has the repo on its crawl list and the `.info` is merged, the core appears in
the in-app Core Downloader on the next build cycle.

## Building it yourself

```sh
make                      # native desktop core -> gloopy_libretro.{dll,so,dylib}
```

Requires a C++17 compiler (on Windows, an MSYS2 MinGW64 shell). The `README.md` "Development"
section has the ARM handheld flags, the Android NDK invocation, and the all-platform
`scripts/build-release.sh`. Release builds **must** define `NDEBUG` (the emulator marks
unimplemented hardware with `assert`, which would otherwise abort the frontend).

## No BIOS, no ROMs

No Casio Loopy BIOS or game data is included, and none may be redistributed with the core. Users
supply their own — see `README.md`.
