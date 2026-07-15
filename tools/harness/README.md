# Test harness

A minimal libretro frontend used to test and benchmark the core without RetroArch.
It loads the core, runs it headlessly, and reports what it saw.

Windows only — it loads the core with `LoadLibrary`.

```sh
make            # produces harness.exe
```

## Usage

```
harness <core.dll> <rom> <system_dir> <out_dir> [mode] [args...] [key=value ...]
```

`system_dir` must contain `loopy_bios.bin` (and `loopy_soundbios.bin` for sound).
No BIOS or ROM is distributed with this repository; you must supply your own.

Any `key=value` argument sets a core option, using the same keys and values as
`libretro_core_options.h` — e.g. `loopy_idle_skip=disabled`, `loopy_frameskip=auto`.

### Modes

| Mode | What it does |
| --- | --- |
| *(none)* | Boots the ROM, then runs the checks below |
| `bench [warmup] [frames]` | Times `retro_run`. Defaults: 600 warmup, 3600 timed. Writes `bench_scene.bmp` so you can confirm what was actually on screen |
| `mouse` | Boots with the Loopy Mouse active |
| `mousetoggle` | Switches the mouse on after boot |
| `advance` | Frame-advance probe |

Core options are applied from boot, except in `bench`, where they are applied
after warmup — so the game reaches the scene under test with every stage of the
pipeline running, and the timed section measures the option, not the load screen.

### What the default mode checks

- The frame is stable and non-blank, and the core reports sane AV info
- Savestate round-trip: size is stable, and restoring a state reproduces the
  same frame
- Determinism: restoring the same state and running a frame twice gives the same
  result both times
- Reset returns the machine to a boot-like state

It also prints `wram_crc` and `audio_crc` — CRC32 fingerprints of work RAM and
of the audio produced. These are the useful ones for verifying an optimization:
two builds that produce identical fingerprints over the same input are running
the same emulation, which a pixel comparison alone cannot show. They are how
frameskip was verified to be output-only, since a skipped frame changes the
picture but must not change the machine.

`harness_underrun=1` makes the harness report a starving audio buffer to the
core on every frame, which is what drives `loopy_frameskip=auto`. Without it,
auto frameskip correctly does nothing.
