# Gloopy — a Casio Loopy libretro core

**Gloopy** is a libretro core for the **Casio Loopy**, for RetroArch and other libretro
frontends. It is a fork of [LoopyMSE](https://github.com/LoopyMSE/LoopyMSE) by PSI and kasami,
whose emulator and reverse engineering it is built on (see [`NOTICES.md`](NOTICES.md)).

**Status:** version 1.2.0 runs the full commercial library. It is provided as-is, and
without warranty. It is GPL v3, so you are free to fork, rebuild and redistribute it.

## Features

- Full-speed Casio Loopy emulation
- Savestates, rewind and run-ahead
- Battery saves, through the frontend's `.srm` files
- Seal printer, stickers you print are saved as PNG
- Loopy Mouse, either with a real mouse or with a gamepad
- *Wanwan Aijou Monogatari*'s expansion sound
- Every screen blend mode, including the 512-wide hi-res mode
- Accurate timing modelled on measurements from a real Loopy

## Installing

1. Download the zip for your platform from the releases page.
2. Put the core file (`gloopy_libretro.dll`, `.so` or `.dylib`) in your frontend's `cores`
   folder and `gloopy_libretro.info` in its `info` folder. On Android, use RetroArch's
   **Load Core → Install or Restore a Core** instead.
3. Add the BIOS files (below).
4. Load a Loopy ROM (`.bin`) with the Gloopy core.

## BIOS

The Loopy cannot boot without its BIOS. **No BIOS is included — you must supply your own.**

| File | Size | Required | CRC32 | MD5 |
| --- | --- | --- | --- | --- |
| `loopy_bios.bin` | 32,768 bytes | **Yes** | `8C57FF9F` | `d527e3ed1bf9bd0661154c202a65c5bd` |
| `loopy_soundbios.bin` | 524,288 bytes | No, but without it there is no sound | `8F51FA17` | `c0f1c899c9ca098663d046d60779711d` |

The filenames are exact (and case-sensitive outside Windows). Put the files in your
frontend's **system** folder, or in a `loopy/` folder inside it. If you are not sure where
that is, putting them **next to your ROMs** works too.

| Frontend | System folder |
| --- | --- |
| RetroArch (Windows) | `RetroArch\system\` |
| RetroArch (macOS) | `~/Library/Application Support/RetroArch/system/` |
| RetroArch (Linux) | `~/.config/retroarch/system/` |
| RetroArch (Android) | `RetroArch/system/` on internal storage |
| Handheld distros (muOS, Knulli, TrimUI, ROCKNIX…) | Usually a top-level `BIOS/` folder on the SD card |

Without a usable main BIOS, content fails to load.

## Controls

### Gamepad

| Loopy Controller | RetroPad |
| --- | --- |
| D-Pad | D-Pad |
| A | B (south) |
| B | A (east) |
| C | Y (west) |
| D | X (north) |
| L | L |
| R | R |
| Start | Start |

### Loopy Mouse

| Loopy Mouse | Mouse | Virtual Mouse (gamepad) |
| --- | --- | --- |
| Move | Move | Left stick, right stick or d-pad |
| Left button | Left button | B (south), L1 or L2 |
| Right button | Right button | A (east), R1 or R2 |

The Loopy has one controller port, so choose what is plugged into it with the **Input
Device** core option: **Controller**, **Mouse** (a real mouse) or **Virtual Mouse** (the
mouse driven from the gamepad). **Choose before loading a game:** Loopy games check the port
only while booting.

## Saves and prints

- **Battery saves** are written by your frontend as `.srm` files.
- **Savestates** also hold the cartridge's save data.
- **Printed seals** are saved as PNG files in your frontend's **save** folder (in RetroArch,
  `Settings → Directory → Save Files`), named by date and time, e.g.
  `loopyseal_20260713_194208_1.png`. 
- ***Wanwan Aijou Monogatari*'s** extra sampled sounds are not in the ROM. Put its `.wav`
  sample set in a `pcm/` folder next to the ROM.

## Core options

| Option | Default | Notes |
| --- | --- | --- |
| Video > Crop Overscan | enabled | Output 224 or 240 lines as the game sets |
| Video > Refresh Rate | 59.83 Hz | The console's hardware rate, or 60 Hz. Restart required |
| Audio > Synth Mix Level | 0.62 | Measured on real hardware; higher is louder but can clip |
| Peripherals > Input Device | Controller | Controller, Mouse or Virtual Mouse; set before loading |
| Peripherals > Mouse Sensitivity | 1x | 0.25x–4x |
| Peripherals > Seal Sticker Format | PNG | PNG or BMP |
| Peripherals > Seal Printer | enabled | Off: games act as if no sticker cartridge is present |
| Performance > Idle Loop Skip | enabled | Leave on: it makes the core 2–3x faster without changing how games play |
| Performance > Frameskip | disabled | For hardware that cannot draw every frame |
| Debugging > Debug Log File | disabled | A session log in the save folder, for homebrew authors. Restart required |

## More

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): building, how the core works, the timing
  model and what has changed from LoopyMSE.
- [`tools/loopy-lab/`](tools/loopy-lab/): the timing and video test ROMs the timing was
  measured with, and their results from a real console.

## License

GNU General Public License, version 3, the same as LoopyMSE. The full text is in `LICENSE`
and the modification notice is in `NOTICES.md`. No copyright is claimed over the
modifications. No BIOS or game data is distributed with this core.
