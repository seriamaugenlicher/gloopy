# Notices and attribution

Gloopy is a Casio Loopy libretro core.

## License and modification notice

Gloopy is free software, licensed under the **GNU General Public License, version 3**
(see `LICENSE` for the full text).

Gloopy is a **modified version of LoopyMSE**. LoopyMSE is licensed under the GPL v3, so
Gloopy is too — the license carries forward, and any further distribution of this code must
also be GPL v3 and ship its complete corresponding source.

The modifications described in `docs/ARCHITECTURE.md` were made in **2026** and are distributed under
the GPL v3. **No copyright is claimed over them.**

This program is distributed in the hope that it will be useful, but **WITHOUT ANY
WARRANTY**; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program.
If not, see <https://www.gnu.org/licenses/>.

## LoopyMSE

Gloopy is a fork of **[LoopyMSE](https://github.com/LoopyMSE/LoopyMSE)**, the Casio
Loopy emulator by **PSI**, with major contributions by **kasami**.

The emulation core in `src/` — the SH-2 interpreter, the VDP, the uPD937 sound synth, the
cartridge and peripheral emulation, and the reverse engineering that made all of it
possible — originates in that project and remains their work. This fork exists to adapt
that emulator to libretro and to run it well on low-power handhelds; it is **not** a
rewrite, and the hard problems were solved upstream.

Licensed under the GNU General Public License v3. See `LICENSE`.

### Relationship to upstream

This is an independent hard fork. Changes made here are **not** submitted back to LoopyMSE.
Please do not report problems with this core to the LoopyMSE project: bugs and behavior here
may not exist upstream, and upstream is not responsible for them.

## Test ROMs

The test ROMs in `tools/loopy-lab/` and their documents are original programs written for this
project; they contain no LoopyMSE code, no BIOS code and no game data. No copyright is claimed
over them, and they may be used, shared and modified freely.

## libretro-common

Portions of [libretro-common](https://github.com/libretro/libretro-common)
(`libretro-common/`, including `libretro.h`) are used under the MIT license,
Copyright (C) 2010-2023 The RetroArch team.

## AI disclosure
Gloopy was ported to libretro and updated with the help of Claude Code. During development,
actual Casio Loopy hardware values were measured and matched whenever possible.

## Casio Loopy

"Casio" and "Loopy" are trademarks of their respective owners. This project is not
affiliated with, endorsed by, or connected to Casio. No copyrighted BIOS or game data is
distributed with this core; the user must supply their own.
