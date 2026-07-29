# Gloopy — a Casio Loopy libretro core

**Gloopy** is a libretro core for the **Casio Loopy**, forked from
[LoopyMSE](https://github.com/LoopyMSE/LoopyMSE) by PSI and kasami.

The emulator itself — the SH-2 interpreter, the VDP, the uPD937 sound synth, and the
reverse engineering behind them — is their work. This fork adapts it to libretro and
tunes it to run at full speed on low-power hardware. See `NOTICES.md` for attribution.

> **This is an independent hard fork.** Changes are not sent upstream to LoopyMSE.

**Status:** version 1.0.0 is feature-complete and runs the full commercial library. It is
provided as-is, without warranty, and is not actively maintained — there is no support or issue
tracker. It's GPL v3, so you are free to fork, rebuild, redistribute, and submit it to
RetroArch's Core Downloader yourself; see [`HANDOFF.md`](HANDOFF.md).

## Features

- Full-speed Casio Loopy emulation, including on handheld ARM hardware
- Savestates, rewind, and run-ahead (deterministic, synchronous core)
- Battery save (SRAM) through the frontend's standard `.srm` handling
- Seal printer emulation: prints are saved as PNG images in the frontend's save directory
- Expansion PCM audio for *Wanwan Aijou Monogatari* 
- Loopy Mouse support, that can be 'hot-swapped' in and out while a game runs

## BIOS

The Loopy boots from its own BIOS, so the core cannot run without one. **No BIOS is
distributed with Gloopy — you must supply your own.**

| File | Size | Required | CRC32 | MD5 |
| --- | --- | --- | --- | --- |
| `loopy_bios.bin` | 32,768 bytes (32 KB) | **Yes** | `8C57FF9F` | `d527e3ed1bf9bd0661154c202a65c5bd` |
| `loopy_soundbios.bin` | 524,288 bytes (512 KB) | No | `8F51FA17` | `c0f1c899c9ca098663d046d60779711d` |

Without `loopy_soundbios.bin` the core still runs, but is **completely silent**.

**The filenames are exact** and are case-sensitive everywhere except Windows. If your
files are named `bios.bin` and `soundbios.bin` (as the standalone LoopyMSE expects), please
rename them. The files have been renamed to avoid collision with other BIOS files in a
multi-platform environment.

### Where to put the BIOS files

The core searches four locations and uses the **first match**, checking each file's size as
it goes:

1. `<system>/loopy_bios.bin`
2. `<system>/loopy/loopy_bios.bin`
3. `<rom folder>/loopy_bios.bin`
4. `<rom folder>/loopy/loopy_bios.bin`

`<system>` is the system/BIOS folder your frontend reports and what most people want to use:

| Frontend | System folder |
| --- | --- |
| RetroArch (Windows) | `RetroArch\system\` |
| RetroArch (macOS) | `~/Library/Application Support/RetroArch/system/` |
| RetroArch (Linux) | `~/.config/retroarch/system/` |
| RetroArch (Android) | `RetroArch/system/` on internal storage |
| Handheld distros (muOS, Knulli, TrimUI, ROCKNIX…) | Usually a top-level `BIOS/` folder on the SD card |

If you don't know where your frontend's system folder is, **putting the BIOS next to your ROMs will work.**

### If the BIOS is missing or wrong

A missing or unusable **main** BIOS is fatal, and content will simply fail to load.

## Controls

### Gamepad

| Loopy | RetroPad |
| --- | --- |
| D-Pad | D-Pad |
| A | B |
| B | A |
| C | Y |
| D | X |
| L | L |
| R | R |
| Start | Start |

### Loopy Mouse

| Loopy Mouse | Your mouse |
| --- | --- |
| Move | Move |
| Left button | Left button |
| Right button | Right button |

The mouse and the gamepad share the Loopy's single controller port, so only one of them is
plugged in at a time — but you can "hot-swap" between them freely while a game is running.
By default, moving the mouse switches input to the mouse, and pressing a gamepad button
switches input to the gamepad. See **Input Device** under [Core options](#core-options).

## Save data

### Battery saves (`.srm`)

Games that save your progress do it to battery-backed SRAM on the cartridge. Your frontend
writes that out as a `.srm` file alongside its other saves.

**These are byte-compatible with standalone LoopyMSE `.sav` files.** If you are coming from
the standalone emulator, rename `YourGame.sav` to `YourGame.srm` and your save carries over.

### Savestates

Fully supported, including **rewind** and **run-ahead** — the core is deterministic, so
these work properly rather than approximately.

Savestates also contain a copy of the cartridge SRAM and are tied to the game they were
made with: a state from a different ROM is refused rather than loaded into the wrong game.

### Printed seals (stickers)

The Loopy's most-famous feature is its built-in thermal printer: several games let you design
a sticker and print it. Gloopy emulates the printer, and **every sticker you print is saved as
a PNG image file.**

**Prints go to your frontend's save folder** — the same folder your `.srm` battery saves and
your savestates go to. In RetroArch that is `Settings → Directory → Save Files`; if you have
not changed it, it is the `saves` folder inside RetroArch:

| Frontend | Prints land in |
| --- | --- |
| RetroArch (Windows) | `RetroArch\saves\` |
| RetroArch (macOS) | `~/Library/Application Support/RetroArch/saves/` |
| RetroArch (Linux) | `~/.config/retroarch/saves/` |
| RetroArch (Android) | RetroArch's save folder on internal storage |
| Handheld distros | The frontend's save folder on the SD card |

Files are named by the date and time you printed them, so they never overwrite each other:

```text
loopyseal_20260713_194208_1.png
loopyseal_20260713_194433_2.png
```

The core logs the exact path when a game loads:

```text
[Printer] prints will be saved to C:\RetroArch-Win64\saves
```
If a print fails, the game itself will tell you (it reports a printer error, just as the
real hardware would), and the log will say why. The most likely cause is a save folder that
does not exist or cannot be written to.

**Peripherals > Seal Sticker Format** can save them as BMP instead, for anything that cannot
read a PNG. PNG is the better choice: a seal is flat-coloured pixel art, which PNG stores
exactly and compresses to a fraction of the size of the equivalent BMP.

Printing can be switched off entirely with the **Peripherals > Seal Printer** core option, in
which case games behave as though no sticker cartridge is present.

### Wanwan expansion audio

*Wanwan Aijou Monogatari* has a cartridge with an extra PCM sound chip whose samples are not
in the ROM dump. To hear them, put the `.wav` sample set in a **`pcm/` folder next to the
ROM**:

```text
roms/
  Wanwan Aijou Monogatari.bin
  pcm/
    src01.wav
    src02.wav
    ...
```

Without it, the game runs and its music plays; only those extra sampled sounds are missing.

## Core options

| Option | Default | Notes |
| --- | --- | --- |
| Video > Crop Overscan | enabled | Output 224 or 240 lines as configured by the game |
| Audio > Synth Mix Level | 0.62 | Level measured on real hardware. Higher values are louder but can clip |
| Peripherals > Input Device | Controller + Mouse | See below |
| Peripherals > Mouse Sensitivity | 1x | 0.25x–4x. Raise it if the cursor feels sluggish |
| Peripherals > Seal Sticker Format | PNG | PNG or BMP |
| Peripherals > Seal Printer | enabled | Restart required |
| Performance > Idle Loop Skip | enabled | Leave this on. See below |
| Performance > Frameskip | disabled | For hardware that cannot draw all 60 frames |

### Input Device

The Loopy has **one controller port**, so its gamepad and its Mouse can never both be
plugged in at once — and a game reads the port to work out which one is there. Games check
this continuously rather than only at startup, so **swapping between them works while a game
is running, with no restart.**

**Controller + Mouse** (the default) 'plugs in' whichever one you touch: move the mouse and the
mouse is plugged in; press a gamepad button and the gamepad is. It behaves exactly as though
you rapidly unplugged one and plugged in the other, because that is what it does. A mouse only
reports movement when it is actually moved, so a mouse sitting still will never take the port
away from your gamepad, and a machine with no mouse at all never swaps.

**Controller only** and **Mouse only** pin the port to one device.

**Mouse Sensitivity** scales how far the Loopy Mouse moves for a given movement of your own.
**1x is tuned to feel right against the real thing** — the Loopy's mouse was a
low-resolution ball mouse, and a modern optical mouse is much more sensitive than the games
were built around. Raise it if the cursor feels sluggish, lower it if it darts about.

Your frontend has no setting for this: libretro hands the core raw movement and leaves the
interpretation to it, so this is the only place it can be adjusted.

Only a handful of games support the Mouse — *PC Collection*, *Little Romance*, *Lupiton's
Wonder Palette* and *Loopy Town* among them. **The rest ignore it entirely**, which is why
**Mouse only** will leave those games with no working input at all. That is the main reason
to prefer the default.

### Idle Loop Skip

Loopy games spend **over 95% of their CPU time** spinning in a loop waiting for the next
frame. This option detects those loops and fast-forwards the emulated CPU through them,
which makes the core roughly 2–3x faster and is what allows it to hold 60fps on low-powered
devices such as emulation handhelds.

The skip only happens where the loop provably cannot observe anything changing before the
next hardware event, so emulation is bit-for-bit identical with it on or off.
It exists as a switch only so that it can be ruled out when troubleshooting.

### Frameskip

For hardware too slow to draw every frame. The emulated machine still runs every frame, so
game speed, input and audio are unaffected — only the picture updates less often.

`auto` drops a frame only when the frontend reports its audio buffer is about to run dry.
The fixed settings always drop a certain number of frames, which gives steadier pacing on
hardware that is consistently too slow.

---

## Development

Everything below concerns building and hacking on the core, not using it.

### Building

```sh
make
```

Produces `gloopy_libretro.dll` / `.so` / `.dylib`. Requires a C++17 compiler; on Windows,
build from an MSYS2 MinGW64 shell (`mingw32-make`).

Handheld ARM builds want:

```sh
make OPTIMIZE="-O3 -flto -mcpu=cortex-a53 -DNDEBUG" OPTIMIZE_LD="-O3 -flto -mcpu=cortex-a53"
```

Objects are kept per build under `obj/<platform>`, so builds for different platforms can
coexist and no `clean` is needed when switching between them. Cross-builds that share a
platform must be told apart with `BUILD_TAG`, since e.g. both Linux cores are
`platform=unix`:

```sh
make platform=unix BUILD_TAG=aarch64 CC=... CXX=...
make platform=unix BUILD_TAG=x86_64  CC=... CXX=...
```

`make clean` removes only the current build; `make clean-all` removes every platform's.

Android needs the NDK: `make platform=android_arm64 ANDROID_NDK=/path/to/ndk` (also
`NDK_HOST=linux-x86_64` if you are not building from Windows).

To build every platform at once into `dist/`, put your toolchain paths in
`scripts/local-env.sh` and run:

```sh
./scripts/build-release.sh                 # all targets
./scripts/build-release.sh linux-aarch64   # or just one
```

### Testing

`tools/harness/` is a small headless libretro frontend used to test and benchmark the core
without RetroArch. It checks savestate round-trips, determinism and reset, times `retro_run`,
and fingerprints work RAM and audio so a change can be proven not to alter emulation. See
`tools/harness/README.md`.

### Differences from LoopyMSE

The core emulation is exactly the same; these are the changes this fork makes.

**Frontend integration** (the core has no SDL, no Boost, and does no file or process I/O
of its own):

- `src/sdl/` (the standalone frontend) is replaced by `libretro.cpp`
- `src/sound/sound.cpp`: the SDL audio device and callback become a synchronous pull-mode
  backend (`Sound::render`); `SDL_LoadWAV`/`SDL_AudioStream` become a small built-in WAV
  reader and resampler
- `src/imgwriter/`: a dependency-free PNG and BMP writer replacing the SDL_image-based one,
  used by the printer. The PNG encoder includes just enough DEFLATE to be standards-
  conformant, so that the core needs neither libpng nor zlib
- `src/printer/printer.cpp`: the external image-viewer launch (`system()`) is removed
- `src/core/cart.cpp`: direct `.sav` writes are removed; SRAM is exposed to the frontend
  via `retro_get_memory_data`
- `src/core/savestate.h`, `system.cpp`: in-memory savestate entry points for
  `retro_serialize` / `retro_unserialize`
- `src/log/`: logging routes into the frontend's logger
- `src/expansion/msm665x/msm665x.cpp`: `SDL_powf` becomes `powf`

**Emulation changes:**

- The sound engine's state is now serialized in savestates.
- The Loopy Mouse is emulated. The plumbing was present upstream but was never fed input.
- Cartridges with a degenerate SRAM header (some *Magical Shop* dumps) load instead of failing.

**Performance** (all verified pixel-exact against the pre-change build across the full
commercial library):

- **Idle loop skip** — see above. The single largest performance win by a wide margin.
- Frameskip — see above.
- SH-2 instruction dispatch uses a 64K jump table instead of a ~112-branch `if`/`else`
  chain, which mispredicted badly on in-order ARM cores.
- The CPU loop consumes fetch-wait cycles arithmetically instead of iterating once per
  emulated clock, and caches the instruction-fetch page to avoid two address translations
  per instruction.
- Decoded caches for the palette and for OAM, rebuilt only when the game writes them
  (previously all 128 object descriptors were re-decoded on every scanline).
- The bitmap, BG and object layer renderers hoist their per-pixel invariants.
- The frame is composited straight into RGB565 and handed to the frontend with no
  conversion pass or staging copy.


## License

Gloopy is licensed under the **GNU General Public License, version 3**, the same license as
LoopyMSE, from which it is derived. GPL v3 code stays GPL v3 — the full license text is in
`LICENSE`, and `NOTICES.md` carries the modification notice.

In short: you may use, study, share and modify this core freely, provided that anything you
distribute based on it is also GPL v3 and ships with its complete corresponding source.

LoopyMSE was modified in 2026 to produce this core. No copyright is claimed over those
modifications. The emulation itself is the work of the LoopyMSE authors — see `NOTICES.md`.

No BIOS or game data is distributed with this core. You must supply your own.
