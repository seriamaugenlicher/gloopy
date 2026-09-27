# Gloopy architecture

How the core is built and how it works. For using it, see [`README.md`](../README.md).

## Building

```sh
make
```

Produces `dist/gloopy_libretro.dll` / `.so` / `.dylib` (`OUTDIR=.` puts it in the repository
root instead, which is what `Makefile.libretro` does for libretro's build-bot). Requires a
C++17 compiler; on Windows, build from an MSYS2 MinGW64 shell (`mingw32-make`). Release builds
must define `NDEBUG`, which the default flags do: the emulator marks unimplemented hardware
with `assert`, which would otherwise abort the frontend.

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

`.github/workflows/build.yml` builds all seven targets on CI, macOS included.

## Layout

- `libretro.cpp`, `libretro_core_options.h`: the libretro frontend layer: options, input,
  video and audio hand-off, savestates, SRAM, BIOS lookup.
- `src/core/`: the SH-1 CPU (`sh2/`, with its on-chip peripherals), memory map, cartridge,
  timing scheduler and savestates.
- `src/video/`: the VDP: layers, blending, capture, and its timing signals.
- `src/sound/`: the uPD937 synth; `src/expansion/`: the *Wanwan* PCM chip.
- `src/printer/`, `src/imgwriter/`: the seal printer and a dependency-free PNG/BMP writer.
- `tools/loopy-lab/`: the test ROMs and their documents.

The core is deterministic and synchronous: `retro_run` emulates exactly one frame, and all
state advances inside the timing scheduler, which is what makes savestates, rewind and
run-ahead exact.

## Frontend integration

The core has no SDL and no Boost, and does no file or process I/O of its own beyond what the
frontend's directories allow:

- `libretro.cpp` is the frontend.
- Audio is a synchronous pull-mode backend (`Sound::render`), 800 samples per frame, with a
  small built-in WAV reader and resampler for the *Wanwan* samples.
- Seal prints go through a PNG and BMP writer with just enough DEFLATE to be
  standards-conformant, so the core needs neither libpng nor zlib.
- Cartridge SRAM is exposed to the frontend via `retro_get_memory_data`; savestates go
  through in-memory `retro_serialize` / `retro_unserialize`.
- Logging routes into the frontend's logger, or into a session log file with **Debug Log
  File**, which also carries serial port 0's output as `[Serial]` lines and reports once when
  a program uses hardware that is not emulated.

### BIOS lookup

The core checks, in order, taking the first file of the right size:

1. `<system>/loopy_bios.bin`
2. `<system>/loopy/loopy_bios.bin`
3. `<rom folder>/loopy_bios.bin`
4. `<rom folder>/loopy/loopy_bios.bin`

and the same for `loopy_soundbios.bin`.

### Input devices

The Loopy's single controller port takes either the gamepad or the Loopy Mouse. A game
chooses its device while booting, by writing the VDP's mode bits, and of the 13 retail titles
11 never check again. Only *I Want a Room in Loopy Town* notices a swap both ways, and
*Lupiton's Wonder Palette* from Mouse to Controller. An automatic swap to whichever device was
last touched was tried and dropped: it almost never reached the game, and a game that had
settled on the mouse saw an *empty* port when a gamepad was swapped in. Hence an explicit
**Input Device** option, read before loading.

**Mouse Sensitivity** 1x is 3/8 of raw movement for Virtual Mouse and 3/16 for a real mouse
(the Loopy's ball mouse had a far lower resolution than a modern optical one). The scale
carries its sub-pixel remainder between frames, so slow movement below 1x does not stall.

### Idle loop skip

Loopy games spend most of their CPU time spinning in a loop waiting for the next frame. The
skip detects a short backward jump to a loop head seen twice within the current slice, with
no memory write, no unrepeatable read, and every register bit-identical, and then
fast-forwards to the next hardware event. It never skips in a delay slot or with an
acceptable interrupt pending. The result is 2–3x faster emulation. The one difference from
running the loop is that its final partial pass is dropped, which can leave work RAM
slightly different in a few games but does not change how they play.

## Timing model

CPU, bus, interrupt, DMA and video timing are modelled on measurements from a real Loopy,
taken with the test ROMs in [`tools/loopy-lab/`](../tools/loopy-lab/). Their documents list
every measurement and the console's values.

- **Frame:** 267,970 CPU cycles, 59.8261 Hz (or exactly 60 Hz with **Video > Refresh
  Rate**). The fractional scanline length is carried rather than rounded.
- **Bus:** every data access takes its bus time: cartridge ROM, SRAM, on-chip peripherals,
  the VDP by memory type, and work RAM by DRAM row (512-byte rows, page-mode hits, the
  write-recovery cycle) with the CBR refresh every 244 cycles.
- **CPU:** on-chip RAM and the BIOS ROM are fetched 32 bits at a time over a memory port
  shared with data accesses; the load-use interlock, branch, multiplier and exception costs
  are as measured. RTE's pops from work RAM take a state more than their reads, except right
  after an instruction that read work RAM itself (a handler's last POP).
- **Interrupts:** measured latency and entry cost; NMI cannot be masked; the VDP's pulses
  are latched until taken, and a level interrupt that stays asserted starves the program.
  IRQ0, IRQ1 and IRQ2 reach the CPU only through their pin functions, IRQ0 at its HCMP
  position. Scanline boundaries, VCOUNT and a live HCOUNT are where the hardware has them.
- **VDP:** bitmap VRAM hold-ups while the VDP fetches, in bursts locked to the line;
  mid-picture BG scroll and layer-enable writes taken per line (per tile for BG scroll Y),
  bitmap scroll, position and enable per line or per frame, palette and backdrop writes
  mid-line.
- **DMA:** bus time and cycle stealing, the raster DMA request (DREQ0) and the transfer-end
  interrupt.
- **Also emulated:** the watchdog timer (interval mode), SLEEP, TAS.B, TRAPA, CPU address
  errors, the ADC channels and the capture interrupt (IRQ2).
- **Registers** read back as a console reads them: reserved bits, register widths,
  clear-only status flags (cleared by writing 0 after reading 1), the VDP's open bus and
  8-bit areas. A byte write to tile VRAM, the palette or OAM fills the word's other byte from
  the bus; a write through the 8-bit view of work RAM (area 1) lands in the word's low byte.

### Against the test ROMs

Gloopy matches 424 of the 470 scored lines of `timing-lab.bin` and `video-lab.bin` exactly or
within 3%. The snow lines are left out, since Gloopy deliberately does not draw snow. The
known differences:

- **Clock drift.** A console's CPU and VDP run from separate crystals, so a test that repeats
  one access at the same beam position lands at a slightly different phase each time; Gloopy's
  frame is exactly 267,970 cycles and lands at the same phase every time. Averages taken that
  way (the bitmap VRAM hold-up sweeps, the refresh-walk histograms) come out as single values
  where the console gives a spread.
- **On-chip RAM branch targets.** A poll loop in on-chip RAM that is entered or loops back at
  an address of the form 4n+2 can land one poll step off, since the extra fetch there is not
  modelled.

## Performance

- **Idle loop skip** (above): the largest win by far.
- **Frameskip:** the emulated machine still runs every frame; only drawing is skipped.
  `auto` drops a frame only when the frontend reports its audio buffer about to run dry.
- SH-1 instruction dispatch is a 64K jump table, not a long `if`/`else` chain, which
  mispredicted badly on in-order ARM cores.
- The CPU loop consumes fetch-wait cycles arithmetically instead of once per emulated clock,
  and caches the instruction-fetch page.
- The palette and OAM are decoded into caches rebuilt only when a game writes them.
- The layer renderers hoist their per-pixel invariants, and the frame is composited straight
  into RGB565 and handed to the frontend with no conversion pass or staging copy.

## Changes from LoopyMSE

The emulator is based on LoopyMSE; this is the record of what was modified in 2026 to make
this core (the GPL's modification notice, see `NOTICES.md`):

- The SDL frontend (`src/sdl/`) is replaced by the libretro layer; audio, image writing,
  logging and SRAM go through the frontend as described above; the printer no longer
  launches an image viewer; `SDL_powf` becomes `powf`.
- The sound engine's state is saved in savestates.
- The Loopy Mouse is emulated (the plumbing was present but never fed input); its buttons
  are active-low.
- Cartridges with a degenerate SRAM header (some *Magical Shop* dumps) load.
- Screen blend modes 2 and 3 (hi-res, delivered 512 pixels wide) are emulated; modes 6 and 7
  show black; in modes 4 and 5 a switched-off top screen is transparent. Capture format 0
  records the blended picture.
- Multi-character tiles and objects wrap within the 8-character grid; bitmap modes 5 to 7
  behave as modes 0 and 1; with Crop Overscan off a 224-line picture is centred.
- Frame timing is exact (upstream ran at about 60.05 Hz), and the timing model above
  replaces upstream's.
- The synth stays silent until its ON button is pressed, and its panel buttons need a press
  held for two frames. Printing fails as on a console when no seal cartridge answers or the
  raster signal is not routed to DMA.
- The performance and timing work above.
