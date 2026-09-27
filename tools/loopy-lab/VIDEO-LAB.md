# Video Lab: a video test ROM for the Casio Loopy

`video-lab.bin` measures what a Casio Loopy's VDP does and when: what its memories cost
the CPU, when it holds the CPU off bitmap VRAM, how it blends, captures, orders and draws
its layers, when register and palette writes reach the picture, and what CPU accesses
during the picture do to it ("snow"). Everything it can see is read back through the
VDP's scanline capture buffer, so what the console draws comes out as numbers; what the
capture cannot see is on picture pages at the end, for photos. The same ROM runs on a
console and in an emulator, so the two can be compared line by line.

It comes in two builds with the same tests and IDs:

- **`video-lab.bin`** runs every test once and shows named values, like `Q1 STRV ORM 131.17
  303.00`: for checking an emulator while developing it, and for regression tests.
- **`video-lab_stress.bin`** runs the whole list **10 times** and shows, for each value, the
  lowest it gave and how far above that the highest was: for baselining hardware. The
  Loopy is not quite deterministic (DRAM refresh, the VDP's own memory cycles and where a
  test starts in the frame move some values), and the **10 runs** column below is that
  range. An emulator's value inside it matches the console.

The values come from one NTSC console, running from a flash cartridge with a pad plugged
in. Every result page was photographed and checked against the checksum printed on it. The
tables give each test two console values: **One run**, what a single run measured, and
**10 runs**, the range the stress build gave. The CPU and interrupt timing tests are in the
companion `timing-lab.bin` (`TIMING-LAB.md`).

## All tests

- **Part 1: VDP access costs**
  - V: VDP memories and registers from the CPU (14 lines)
  - D: DMA into VRAM (3 lines)
- **Part 2: bitmap VRAM hold-ups**
  - B: by position in the line (20 lines)
  - H, I, R: a NOP at a time (83 lines)
- **Part 3: what the VDP draws**
  - Y: blend modes (14 lines)
  - C: the capture formats (7 lines)
  - Q: layer priority (16 lines)
  - M: bitmap modes (16 lines)
  - T: background layouts and tile sizes (32 lines)
  - J: objects (7 lines)
- **Part 4: when changes reach the picture**
  - L: scroll and palette writes at a line's start (3 lines)
  - W: writes in the middle of a line (12 lines)
  - X: bitmap scroll X in horizontal blanking (4 lines)
  - F: flash write (3 lines)
  - G: picture height (6 lines)
- **Part 5: snow**
  - N and P: accesses kept up over a line (6 lines)
  - S and U: one access at a set point (28 lines)
  - A, K and O: where in a line an access snows (36 lines)

## Running it

- **On a console:** flash `video-lab.bin` (or `video-lab_stress.bin`) to a cartridge. Plug a pad in and press nothing
  until the result pages appear: about 35 seconds, or five and a half minutes for the stress build (whose header shows `RUN nn/10` while it measures). **D-pad left and right turn the pages**; until the first
  press they turn by themselves every 6 seconds. Each result page ends with a `PAGE CRC`,
  a CRC-32 of its lines, so a photo can be checked on its own. The seven picture pages
  come after the result pages. Nothing is written to cartridge SRAM.
- **In an emulator:** run it for 2000 frames (20000 for the stress build). The results are at work RAM `0x09070000`,
  as for `timing-lab.bin`.

## Reading a line

In the plain build each line is `ID NAME value value`, the ID being the section's letter and a
running number. In the stress build it is `ID lo +d lo +d`: the ID, then for each of the line's two values the lowest the 10 runs gave and how much
higher the highest was. Hex readings show their lowest and highest. The tables give each
test's name and what its two values are.

In the plain build a mark left of each ID judges the line against the console's 10-run
range: a green `O` when every value lies inside it, a red `X` when one does not. Hex readings
are judged as whole values and ADC readings within 8 counts; a median's spread is not judged.
The snow lines (sections A, K and O) get no mark: each word holds two fields, the HCOUNT and
what changed, and the stress build keeps only each word's lowest and highest, not each
field's. The marks are drawn on screen only, so the page checksums do not cover them.

- **Cycle counts** are in CPU cycles (16 MHz), each the median of 7 repeats.
- **HCOUNT** runs -84 to 257 a line, one count every 4 VDP clocks (about 3 CPU cycles);
  the picture's first pixel is HCOUNT 0, and a line starts at -84, with horizontal
  blanking.
- **Pixels** are palette indices (8-bit capture) or 15-bit colours (16-bit capture), in hex.
- Every capture is of one line of screen A unless the section says otherwise.

## Part 1: VDP access costs

### V: VDP memories and registers from the CPU

One access, from cartridge ROM code (so each includes its 3-cycle fetch), during the
picture (`ACT`) or vertical blanking (`VBL`): cycles per access and the spread. `FBM` is
bitmap VRAM with BM_MEM_CTRL's fast mode on.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| V1 | `BMP RD ACT` | 8.11 (spread +0.01) | 8.11 +0 | Read of bitmap VRAM, during the picture |
| V2 | `BMP WR ACT` | 6.19 (spread +0.01) | 6.18 +0.01 | Write of bitmap VRAM, during the picture |
| V3 | `BMP RD VBL` | 8.00 | 8 +0 | Read of bitmap VRAM, in vertical blanking |
| V4 | `BMP WR VBL` | 6.03 | 6.03 +0 | Write of bitmap VRAM, in vertical blanking |
| V5 | `TIL RD ACT` | 10.00 | 10 +0 | Read of tile VRAM, during the picture |
| V6 | `TIL WR ACT` | 6.03 | 6.03 +0 | Write of tile VRAM, during the picture |
| V7 | `TIL WR VBL` | 6.03 | 6.03 +0 | Write of tile VRAM, in vertical blanking |
| V8 | `PAL WR ACT` | 6.03 | 6.03 +0 | Write of palette, during the picture |
| V9 | `PAL RD ACT` | 7.00 | 7 +0 | Read of palette, during the picture |
| VA | `OAM WR ACT` | 6.03 | 6.03 +0 | Write of OAM, during the picture |
| VB | `REG RD ACT` | 7.00 | 7 +0 | Read of a VDP register, during the picture |
| VC | `REG WR ACT` | 6.03 | 6.03 +0 | Write of a VDP register, during the picture |
| VD | `FBM RD VBL` | 7.00 | 7 +0 | Read of bitmap VRAM in fast mode, in vertical blanking |
| VE | `FBM WR VBL` | 5.03 | 5.03 +0 | Write of bitmap VRAM in fast mode, in vertical blanking |

### D: DMA into VRAM

DMA channel 3, auto-request, from work RAM: cycles per unit.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| D1 | `WB VBL` | 4.09 | 4.09 +0.02 | Work RAM to bitmap VRAM, in vertical blanking |
| D2 | `WB ACT` | 4.24 | 4.24 +0.02 | Work RAM to bitmap VRAM, during the picture |
| D3 | `WT VBL` | 6.07 | 6.05 +0.02 | Work RAM to tile VRAM, in vertical blanking |

## Part 2: bitmap VRAM hold-ups

With a bitmap layer on (or not), the VDP holds CPU accesses to bitmap VRAM off while it
fetches. A single access waits somewhere between nothing and a few cycles, depending on
where in the VDP's memory cycle it lands, so these are statistics of a near-random wait;
they move from run to run.

### B: by position in the line

One access at a set delay after the line boundary (`Knn`: delay-loop passes of 5 cycles),
16 picture lines: HCOUNT at the access, then the mean wait in cycles against the same
access in vertical blanking. `RD+L`: with bitmap layer 0 shown.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| B1 | `RD K00` | -73.00 / 2.31 | -73 +1 / 2.19 +1.25 | Read, delay 00: HCOUNT, mean wait |
| B2 | `RD K02` | -68.00 / 2.94 | -69 +2 / 2.38 +1.18 | Read, delay 02: HCOUNT, mean wait |
| B3 | `RD K04` | -66.00 / 0.88 | -65 +0 / 1.31 +1.82 | Read, delay 04: HCOUNT, mean wait |
| B4 | `RD K06` | -62.00 / 2.13 | -62 +0 / 1.75 +0.94 | Read, delay 06: HCOUNT, mean wait |
| B5 | `RD K08` | -60.00 / 0.63 | -60 +0 / 0.50 +0.56 | Read, delay 08: HCOUNT, mean wait |
| B6 | `RD K10` | -57.00 / 0.00 | -58 +2 / 0 +0 | Read, delay 10: HCOUNT, mean wait |
| B7 | `RD K12` | -53.00 / 0.00 | -54 +2 / 0 +0 | Read, delay 12: HCOUNT, mean wait |
| B8 | `RD K14` | -50.00 / 0.00 | -50 +1 / 0 +0 | Read, delay 14: HCOUNT, mean wait |
| B9 | `WR K00` | -73.00 / 1.88 | -73 +0 / 1.50 +1.25 | Write, delay 00: HCOUNT, mean wait |
| BA | `WR K02` | -70.00 / 1.94 | -70 +1 / 1.75 +0.94 | Write, delay 02: HCOUNT, mean wait |
| BB | `WR K04` | -66.00 / 2.50 | -66 +0 / 2.06 +1.19 | Write, delay 04: HCOUNT, mean wait |
| BC | `WR K06` | -63.00 / 2.75 | -63 +0 / 1.94 +1.62 | Write, delay 06: HCOUNT, mean wait |
| BD | `WR K08` | -60.00 / 0.06 | -61 +1 / 0.69 +0.75 | Write, delay 08: HCOUNT, mean wait |
| BE | `WR K10` | -56.00 / 0.00 | -58 +1 / 0 +0.13 | Write, delay 10: HCOUNT, mean wait |
| BF | `WR K12` | -54.00 / 0.19 | -54 +1 / 0 +0.19 | Write, delay 12: HCOUNT, mean wait |
| BG | `WR K14` | -51.00 / 0.00 | -51 +2 / 0 +0.19 | Write, delay 14: HCOUNT, mean wait |
| BH | `RD+L K12` | -53.00 / 0.00 | -54 +2 / 0 +0 | Read, delay 12, bitmap layer shown: HCOUNT, mean wait |
| BI | `RD+L K18` | -43.00 / 0.00 | -43 +0 / 0 +0 | Read, delay 18, bitmap layer shown: HCOUNT, mean wait |
| BJ | `RD+L K24` | -33.00 / 0.00 | -33 +1 / 0 +0 | Read, delay 24, bitmap layer shown: HCOUNT, mean wait |
| BK | `RD+L K30` | -23.00 / 0.00 | -23 +1 / 0 +0 | Read, delay 30, bitmap layer shown: HCOUNT, mean wait |

### H, I, R: a NOP at a time

The same, one access `Nnn` NOPs after the line boundary: H reads from on-chip RAM code, I
writes from on-chip RAM code (a NOP 1 cycle), R reads from ROM code (a NOP 3 cycles). The
first line of each gives HCOUNT at the first and the last point; the others the mean and
the largest wait over 16 lines.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| H1 | `ORD H0/HN` | -72.00 / -53.00 | -74 +2 / -53 +1 | On-chip RAM code read sweep: HCOUNT at its first / last point |
| H2 | `ORD N00` | 1.44 / 4.00 | 1.69 +1 / 4 +1 | On-chip RAM code read, 0 NOPs after the line boundary: mean / largest wait |
| H3 | `ORD N02` | 1.50 / 5.00 | 1.19 +1 / 5 +0 | On-chip RAM code read, 2 NOPs after the line boundary: mean / largest wait |
| H4 | `ORD N04` | 2.69 / 5.00 | 1.38 +1.75 / 4 +1 | On-chip RAM code read, 4 NOPs after the line boundary: mean / largest wait |
| H5 | `ORD N06` | 2.63 / 5.00 | 1.94 +1.06 / 5 +0 | On-chip RAM code read, 6 NOPs after the line boundary: mean / largest wait |
| H6 | `ORD N08` | 2.50 / 5.00 | 1.63 +1.25 / 5 +0 | On-chip RAM code read, 8 NOPs after the line boundary: mean / largest wait |
| H7 | `ORD N0A` | 2.06 / 4.00 | 1.44 +0.62 / 3 +2 | On-chip RAM code read, 10 NOPs after the line boundary: mean / largest wait |
| H8 | `ORD N0C` | 0.88 / 5.00 | 0.88 +0.68 / 5 +0 | On-chip RAM code read, 12 NOPs after the line boundary: mean / largest wait |
| H9 | `ORD N0E` | 1.88 / 5.00 | 1.50 +0.75 / 5 +0 | On-chip RAM code read, 14 NOPs after the line boundary: mean / largest wait |
| HA | `ORD N10` | 2.13 / 5.00 | 2.06 +0.88 / 5 +0 | On-chip RAM code read, 16 NOPs after the line boundary: mean / largest wait |
| HB | `ORD N12` | 2.44 / 5.00 | 2.13 +1.18 / 5 +0 | On-chip RAM code read, 18 NOPs after the line boundary: mean / largest wait |
| HC | `ORD N14` | 1.63 / 4.00 | 0.88 +1 / 3 +2 | On-chip RAM code read, 20 NOPs after the line boundary: mean / largest wait |
| HD | `ORD N16` | 1.63 / 5.00 | 1.44 +0.62 / 5 +0 | On-chip RAM code read, 22 NOPs after the line boundary: mean / largest wait |
| HE | `ORD N18` | 2.56 / 5.00 | 2.06 +1.07 / 5 +0 | On-chip RAM code read, 24 NOPs after the line boundary: mean / largest wait |
| HF | `ORD N1A` | 2.50 / 5.00 | 1.69 +2 / 5 +0 | On-chip RAM code read, 26 NOPs after the line boundary: mean / largest wait |
| HG | `ORD N1C` | 1.56 / 4.00 | 1.44 +0.69 / 3 +2 | On-chip RAM code read, 28 NOPs after the line boundary: mean / largest wait |
| HH | `ORD N1E` | 0.63 / 4.00 | 0.75 +1.50 / 5 +0 | On-chip RAM code read, 30 NOPs after the line boundary: mean / largest wait |
| HI | `ORD N20` | 1.94 / 5.00 | 1.88 +0.68 / 4 +1 | On-chip RAM code read, 32 NOPs after the line boundary: mean / largest wait |
| HJ | `ORD N22` | 1.94 / 5.00 | 2.06 +0.75 / 4 +1 | On-chip RAM code read, 34 NOPs after the line boundary: mean / largest wait |
| HK | `ORD N24` | 1.75 / 5.00 | 1.88 +1 / 5 +0 | On-chip RAM code read, 36 NOPs after the line boundary: mean / largest wait |
| HL | `ORD N26` | 0.56 / 3.00 | 0.63 +0.87 / 3 +2 | On-chip RAM code read, 38 NOPs after the line boundary: mean / largest wait |
| HM | `ORD N28` | 0.94 / 4.00 | 0.56 +0.57 / 3 +2 | On-chip RAM code read, 40 NOPs after the line boundary: mean / largest wait |
| HN | `ORD N2A` | 0.63 / 3.00 | 0.13 +0.50 / 1 +2 | On-chip RAM code read, 42 NOPs after the line boundary: mean / largest wait |
| HO | `ORD N2C` | 0.06 / 1.00 | 0 +0.19 / 0 +1 | On-chip RAM code read, 44 NOPs after the line boundary: mean / largest wait |
| HP | `ORD N2E` | 0.00 / 0.00 | 0 +0 / 0 +0 | On-chip RAM code read, 46 NOPs after the line boundary: mean / largest wait |
| HQ | `ORD N30` | 0.00 / 0.00 | 0 +0 / 0 +0 | On-chip RAM code read, 48 NOPs after the line boundary: mean / largest wait |
| HR | `ORD N32` | 0.00 / 0.00 | 0 +0 / 0 +0 | On-chip RAM code read, 50 NOPs after the line boundary: mean / largest wait |
| HS | `ORD N34` | 0.00 / 0.00 | 0 +0 / 0 +0 | On-chip RAM code read, 52 NOPs after the line boundary: mean / largest wait |
| HT | `ORD N36` | 0.00 / 0.00 | 0 +0 / 0 +0 | On-chip RAM code read, 54 NOPs after the line boundary: mean / largest wait |
| HU | `ORD N38` | 0.00 / 0.00 | 0 +0 / 0 +0 | On-chip RAM code read, 56 NOPs after the line boundary: mean / largest wait |
| HV | `ORD N3A` | 0.00 / 0.00 | 0 +0 / 0 +0 | On-chip RAM code read, 58 NOPs after the line boundary: mean / largest wait |
| HW | `ORD N3C` | 0.00 / 0.00 | 0 +0 / 0 +0 | On-chip RAM code read, 60 NOPs after the line boundary: mean / largest wait |
| HX | `ORD N3E` | 0.00 / 0.00 | 0 +0 / 0 +0 | On-chip RAM code read, 62 NOPs after the line boundary: mean / largest wait |
| I1 | `OWR H0/HN` | -73.00 / -53.00 | -73 +0 / -54 +1 | On-chip RAM code write sweep: HCOUNT at its first / last point |
| I2 | `OWR N00` | 2.56 / 5.00 | 1.50 +1.25 / 4 +1 | On-chip RAM code write, 0 NOPs after the line boundary: mean / largest wait |
| I3 | `OWR N02` | 3.31 / 8.00 | 2.06 +0.82 / 4 +3 | On-chip RAM code write, 2 NOPs after the line boundary: mean / largest wait |
| I4 | `OWR N04` | 0.94 / 4.00 | 1 +1 / 3 +2 | On-chip RAM code write, 4 NOPs after the line boundary: mean / largest wait |
| I5 | `OWR N06` | 1.94 / 5.00 | 1.50 +1.06 / 4 +3 | On-chip RAM code write, 6 NOPs after the line boundary: mean / largest wait |
| I6 | `OWR N08` | 3.31 / 5.00 | 1.75 +1.31 / 5 +0 | On-chip RAM code write, 8 NOPs after the line boundary: mean / largest wait |
| I7 | `OWR N0A` | 3.50 / 5.00 | 2.50 +0.81 / 5 +0 | On-chip RAM code write, 10 NOPs after the line boundary: mean / largest wait |
| I8 | `OWR N0C` | 1.88 / 4.00 | 1.31 +0.88 / 3 +3 | On-chip RAM code write, 12 NOPs after the line boundary: mean / largest wait |
| I9 | `OWR N0E` | 1.44 / 5.00 | 0.88 +0.81 / 4 +1 | On-chip RAM code write, 14 NOPs after the line boundary: mean / largest wait |
| IA | `OWR N10` | 1.56 / 5.00 | 0.94 +1.12 / 4 +1 | On-chip RAM code write, 16 NOPs after the line boundary: mean / largest wait |
| IB | `OWR N12` | 2.75 / 5.00 | 2.75 +1.44 / 5 +1 | On-chip RAM code write, 18 NOPs after the line boundary: mean / largest wait |
| IC | `OWR N14` | 2.31 / 5.00 | 2.13 +0.93 / 4 +1 | On-chip RAM code write, 20 NOPs after the line boundary: mean / largest wait |
| ID | `OWR N16` | 0.81 / 3.00 | 0.50 +1 / 3 +3 | On-chip RAM code write, 22 NOPs after the line boundary: mean / largest wait |
| IE | `OWR N18` | 1.50 / 5.00 | 1.38 +0.93 / 5 +0 | On-chip RAM code write, 24 NOPs after the line boundary: mean / largest wait |
| IF | `OWR N1A` | 2.88 / 5.00 | 2.13 +1.37 / 5 +2 | On-chip RAM code write, 26 NOPs after the line boundary: mean / largest wait |
| IG | `OWR N1C` | 2.25 / 5.00 | 1.25 +2.19 / 3 +3 | On-chip RAM code write, 28 NOPs after the line boundary: mean / largest wait |
| IH | `OWR N1E` | 1.56 / 8.00 | 1.63 +0.68 / 4 +1 | On-chip RAM code write, 30 NOPs after the line boundary: mean / largest wait |
| II | `OWR N20` | 2.13 / 5.00 | 1.50 +1 / 5 +0 | On-chip RAM code write, 32 NOPs after the line boundary: mean / largest wait |
| IJ | `OWR N22` | 2.75 / 5.00 | 1.75 +0.69 / 4 +1 | On-chip RAM code write, 34 NOPs after the line boundary: mean / largest wait |
| IK | `OWR N24` | 1.81 / 5.00 | 1.38 +1.25 / 4 +1 | On-chip RAM code write, 36 NOPs after the line boundary: mean / largest wait |
| IL | `OWR N26` | 2.06 / 5.00 | 1.94 +1.25 / 5 +0 | On-chip RAM code write, 38 NOPs after the line boundary: mean / largest wait |
| IM | `OWR N28` | 0.94 / 4.00 | 0.50 +0.69 / 3 +1 | On-chip RAM code write, 40 NOPs after the line boundary: mean / largest wait |
| IN | `OWR N2A` | 0.56 / 4.00 | 0.25 +0.25 / 2 +1 | On-chip RAM code write, 42 NOPs after the line boundary: mean / largest wait |
| IO | `OWR N2C` | 0.13 / 2.00 | 0 +0.25 / 0 +3 | On-chip RAM code write, 44 NOPs after the line boundary: mean / largest wait |
| IP | `OWR N2E` | 0.00 / 0.00 | 0 +0.19 / 0 +3 | On-chip RAM code write, 46 NOPs after the line boundary: mean / largest wait |
| IQ | `OWR N30` | 0.00 / 0.00 | 0 +0.19 / 0 +3 | On-chip RAM code write, 48 NOPs after the line boundary: mean / largest wait |
| IR | `OWR N32` | 0.00 / 0.00 | 0 +0.13 / 0 +2 | On-chip RAM code write, 50 NOPs after the line boundary: mean / largest wait |
| IS | `OWR N34` | 0.00 / 0.00 | 0 +0.06 / 0 +1 | On-chip RAM code write, 52 NOPs after the line boundary: mean / largest wait |
| IT | `OWR N36` | 0.00 / 0.00 | 0 +0 / 0 +0 | On-chip RAM code write, 54 NOPs after the line boundary: mean / largest wait |
| IU | `OWR N38` | 0.19 / 3.00 | 0 +0.13 / 0 +2 | On-chip RAM code write, 56 NOPs after the line boundary: mean / largest wait |
| IV | `OWR N3A` | 0.00 / 0.00 | 0 +0.06 / 0 +1 | On-chip RAM code write, 58 NOPs after the line boundary: mean / largest wait |
| IW | `OWR N3C` | 0.19 / 3.00 | 0 +0.19 / 0 +3 | On-chip RAM code write, 60 NOPs after the line boundary: mean / largest wait |
| IX | `OWR N3E` | 0.13 / 2.00 | 0 +0.13 / 0 +2 | On-chip RAM code write, 62 NOPs after the line boundary: mean / largest wait |
| R1 | `RRD H0/HN` | -61.00 / -49.00 | -66 +5 / -49 +1 | ROM code read sweep: HCOUNT at its first / last point |
| R2 | `RRD N00` | 1.81 / 5.00 | 1.06 +1.13 / 5 +0 | ROM code read, 0 NOPs after the line boundary: mean / largest wait |
| R3 | `RRD N01` | 2.13 / 5.00 | 1.19 +0.87 / 4 +1 | ROM code read, 1 NOPs after the line boundary: mean / largest wait |
| R4 | `RRD N02` | 1.81 / 4.00 | 1.75 +1.06 / 4 +1 | ROM code read, 2 NOPs after the line boundary: mean / largest wait |
| R5 | `RRD N03` | 1.31 / 4.00 | 1.31 +1.13 / 4 +1 | ROM code read, 3 NOPs after the line boundary: mean / largest wait |
| R6 | `RRD N04` | 2.69 / 5.00 | 2.06 +0.44 / 5 +0 | ROM code read, 4 NOPs after the line boundary: mean / largest wait |
| R7 | `RRD N05` | 1.69 / 5.00 | 1.88 +0.81 / 5 +0 | ROM code read, 5 NOPs after the line boundary: mean / largest wait |
| R8 | `RRD N06` | 1.94 / 5.00 | 0.88 +1.68 / 3 +2 | ROM code read, 6 NOPs after the line boundary: mean / largest wait |
| R9 | `RRD N07` | 1.50 / 5.00 | 0.88 +1.43 / 3 +2 | ROM code read, 7 NOPs after the line boundary: mean / largest wait |
| RA | `RRD N08` | 1.25 / 5.00 | 0.38 +1.25 / 2 +3 | ROM code read, 8 NOPs after the line boundary: mean / largest wait |
| RB | `RRD N09` | 1.00 / 5.00 | 0.19 +2.56 / 2 +3 | ROM code read, 9 NOPs after the line boundary: mean / largest wait |
| RC | `RRD N0A` | 0.00 / 0.00 | 0 +0.31 / 0 +2 | ROM code read, 10 NOPs after the line boundary: mean / largest wait |
| RD | `RRD N0B` | 0.00 / 0.00 | 0 +0 / 0 +0 | ROM code read, 11 NOPs after the line boundary: mean / largest wait |
| RE | `RRD N0C` | 0.00 / 0.00 | 0 +0 / 0 +0 | ROM code read, 12 NOPs after the line boundary: mean / largest wait |
| RF | `RRD N0D` | 0.00 / 0.00 | 0 +0 / 0 +0 | ROM code read, 13 NOPs after the line boundary: mean / largest wait |
| RG | `RRD N0E` | 0.00 / 0.00 | 0 +0 / 0 +0 | ROM code read, 14 NOPs after the line boundary: mean / largest wait |
| RH | `RRD N0F` | 0.00 / 0.00 | 0 +0 / 0 +0 | ROM code read, 15 NOPs after the line boundary: mean / largest wait |

## Part 3: what the VDP draws

### Y: blend modes

Two backdrop colours, no layers, blended through capture format 0 (15-bit colour, hex):
additive and subtractive (`+/-`), a second colour pair, modes 2-7, each screen switched
off (`-A/-B`), the SBCOL bit, and modes 4 and 5 over a layer.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| Y1 | `BL0 +/-` | 7F3F / 01F1 | 7F3F 7F3F / 01F1 01F1 | Blend mode 0: add / subtract |
| Y2 | `BL1 +/-` | 4595 / 00E8 | 4595 4595 / 00E8 00E8 | Blend mode 1: add / subtract |
| Y3 | `BL0 +/- 2` | 1FFF / 001D | 1FFF 1FFF / 001D 001D | Blend mode 0, second colour pair: add / subtract |
| Y4 | `BL1 +/- 2` | 0E50 / 000E | 0E50 0E50 / 000E 000E | Blend mode 1, second colour pair: add / subtract |
| Y5 | `BL2/BL3` | 2A9E / 64AD | 2A9E 2A9E / 64AD 64AD | Blend modes 2 / 3 (A only, hi-res): the pixel |
| Y6 | `BL4/BL5` | 2A9E / 64AD | 2A9E 2A9E / 64AD 64AD | Blend modes 4 / 5 (one screen over the other): the pixel |
| Y7 | `BL6/BL7` | 0000 / 0000 | 0000 0000 / 0000 0000 | Blend modes 6 / 7 (invalid): the pixel |
| Y8 | `BL0 -A/-B` | 64AD / 2A9E | 64AD 64AD / 2A9E 2A9E | Blend mode 0: screen A off / screen B off |
| Y9 | `BL1 -A/-B` | 3046 / 154F | 3046 3046 / 154F 154F | Blend mode 1: screen A off / screen B off |
| YA | `BL4 -A/-B` | 0000 / 2A9E | 0000 0000 / 2A9E 2A9E | Blend mode 4: screen A off / screen B off |
| YB | `BL5 -A/-B` | 64AD / 0000 | 64AD 64AD / 0000 0000 | Blend mode 5: screen A off / screen B off |
| YC | `SBCOL 0/4` | 7F3F / 64AD | 7F3F 7F3F / 64AD 64AD | SBCOL set, blend mode 0 / 4: the pixel |
| YD | `BL4 LAY/BD` | 7FE0 / 2A9E | 7FE0 7FE0 / 2A9E 2A9E | Mode 4 over a layer: the layer's pixel / the backdrop beside it |
| YE | `BL5 LAY/BD` | 7FE0 / 64AD | 7FE0 7FE0 / 64AD 64AD | Mode 5 over a layer: the layer's pixel / the backdrop beside it |

### C: the capture formats

One scene with every layer, an additive blend and both backdrops, captured in formats 0-3:
the CRC-32 of the whole buffer (high and low halves), then single pixels, and format 0 in
hi-res.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| C1 | `CAP F0 CRC` | 15CF / 7336 | 15CF 15CF / 7336 7336 | Format 0: CRC-32 of the buffer, high / low half |
| C2 | `CAP F1 CRC` | 588E / 10DE | 588E 588E / 10DE 10DE | Format 1: CRC-32 of the buffer, high / low half |
| C3 | `CAP F2 CRC` | DF4B / 16DF | DF4B DF4B / 16DF 16DF | Format 2: CRC-32 of the buffer, high / low half |
| C4 | `CAP F3 CRC` | DF4B / 16DF | DF4B DF4B / 16DF 16DF | Format 3: CRC-32 of the buffer, high / low half |
| C5 | `CAP F0 PX` | 7F5F / 7F3F | 7F5F 7F5F / 7F3F 7F3F | Two pixels |
| C6 | `CAP F1 PX` | 56B5 / 2A9E | 56B5 56B5 / 2A9E 2A9E | Two pixels |
| C7 | `CAP F0 HIR` | 64AD / 64AD | 64AD 64AD / 64AD 64AD | The pixel and x = 0 in hi-res |

### Q: layer priority

All eight layers on screen A, each with one pixel on the same spot. For each of the 16
priority settings, the order of the layers from the front, a nibble each: 0 BG0, 1 BG1, 2-5
BM0-3, 6 OBJ0, 7 OBJ1, F none left (found by capturing, switching the front layer off, and
again).

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| Q1 | `PRIO 0` | 6723 / 4501 | 6723 6723 / 4501 4501 | Priority setting 0: layer order from the front, two nibble groups |
| Q2 | `PRIO 1` | 6745 / 2301 | 6745 6745 / 2301 2301 | Priority setting 1: layer order from the front, two nibble groups |
| Q3 | `PRIO 2` | 6702 / 3451 | 6702 6702 / 3451 3451 | Priority setting 2: layer order from the front, two nibble groups |
| Q4 | `PRIO 3` | 6704 / 5231 | 6704 6704 / 5231 5231 | Priority setting 3: layer order from the front, two nibble groups |
| Q5 | `PRIO 4` | 7236 / 4501 | 7236 7236 / 4501 4501 | Priority setting 4: layer order from the front, two nibble groups |
| Q6 | `PRIO 5` | 7456 / 2301 | 7456 7456 / 2301 2301 | Priority setting 5: layer order from the front, two nibble groups |
| Q7 | `PRIO 6` | 7023 / 6451 | 7023 7023 / 6451 6451 | Priority setting 6: layer order from the front, two nibble groups |
| Q8 | `PRIO 7` | 7045 / 6231 | 7045 7045 / 6231 6231 | Priority setting 7: layer order from the front, two nibble groups |
| Q9 | `PRIO 8` | 7234 / 5601 | 7234 7234 / 5601 5601 | Priority setting 8: layer order from the front, two nibble groups |
| QA | `PRIO 9` | 7452 / 3601 | 7452 7452 / 3601 3601 | Priority setting 9: layer order from the front, two nibble groups |
| QB | `PRIO 10` | 7023 / 4561 | 7023 7023 / 4561 4561 | Priority setting 10: layer order from the front, two nibble groups |
| QC | `PRIO 11` | 7045 / 2361 | 7045 7045 / 2361 2361 | Priority setting 11: layer order from the front, two nibble groups |
| QD | `PRIO 12` | 7234 / 5016 | 7234 7234 / 5016 5016 | Priority setting 12: layer order from the front, two nibble groups |
| QE | `PRIO 13` | 7452 / 3016 | 7452 7452 / 3016 3016 | Priority setting 13: layer order from the front, two nibble groups |
| QF | `PRIO 14` | 7023 / 4516 | 7023 7023 / 4516 4516 | Priority setting 14: layer order from the front, two nibble groups |
| QG | `PRIO 15` | 7045 / 2316 | 7045 7045 / 2316 2316 | Priority setting 15: layer order from the front, two nibble groups |

### M: bitmap modes

BM0 alone in bitmap mode 0-7, bitmap VRAM filled so every byte says where it is, at two
scroll settings (`S0`, `S1`): the indices shown at x = 0 and 200 on line 20.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| M1 | `BMM0 S0` | 1414 / 00C8 | 1414 1414 / 00C8 00C8 | Bitmap mode 0, scroll setting 0: indices at x 0 / 200 |
| M2 | `BMM0 S1` | B1B1 / 00C8 | B1B1 B1B1 / 00C8 00C8 | Bitmap mode 0, scroll setting 1: indices at x 0 / 200 |
| M3 | `BMM1 S0` | 1414 / 00C8 | 1414 1414 / 00C8 00C8 | Bitmap mode 1, scroll setting 0: indices at x 0 / 200 |
| M4 | `BMM1 S1` | B1B1 / 00C8 | B1B1 B1B1 / 00C8 00C8 | Bitmap mode 1, scroll setting 1: indices at x 0 / 200 |
| M5 | `BMM2 S0` | 1111 / 0016 | 1111 1111 / 0016 0016 | Bitmap mode 2, scroll setting 0: indices at x 0 / 200 |
| M6 | `BMM2 S1` | 1B1B / 181E | 1B1B 1B1B / 181E 181E | Bitmap mode 2, scroll setting 1: indices at x 0 / 200 |
| M7 | `BMM3 S0` | 1111 / 0016 | 1111 1111 / 0016 0016 | Bitmap mode 3, scroll setting 0: indices at x 0 / 200 |
| M8 | `BMM3 S1` | 1B1B / 181E | 1B1B 1B1B / 181E 181E | Bitmap mode 3, scroll setting 1: indices at x 0 / 200 |
| M9 | `BMM4 S0` | 1111 / 0016 | 1111 1111 / 0016 0016 | Bitmap mode 4, scroll setting 0: indices at x 0 / 200 |
| MA | `BMM4 S1` | 1B1B / 181E | 1B1B 1B1B / 181E 181E | Bitmap mode 4, scroll setting 1: indices at x 0 / 200 |
| MB | `BMM5 S0` | 1414 / 00C8 | 1414 1414 / 00C8 00C8 | Bitmap mode 5, scroll setting 0: indices at x 0 / 200 |
| MC | `BMM5 S1` | B1B1 / 00C8 | B1B1 B1B1 / 00C8 00C8 | Bitmap mode 5, scroll setting 1: indices at x 0 / 200 |
| MD | `BMM6 S0` | 1414 / 00C8 | 1414 1414 / 00C8 00C8 | Bitmap mode 6, scroll setting 0: indices at x 0 / 200 |
| ME | `BMM6 S1` | B1B1 / 00C8 | B1B1 B1B1 / 00C8 00C8 | Bitmap mode 6, scroll setting 1: indices at x 0 / 200 |
| MF | `BMM7 S0` | 1414 / 00C8 | 1414 1414 / 00C8 00C8 | Bitmap mode 7, scroll setting 0: indices at x 0 / 200 |
| MG | `BMM7 S1` | B1B1 / 00C8 | B1B1 B1B1 / 00C8 00C8 | Bitmap mode 7, scroll setting 1: indices at x 0 / 200 |

### T: background layouts and tile sizes

BG0 at 8bpp in layout 0-7 with 8, 16, 32 or 64-pixel tiles, each map cell naming its
character by row and column, scrolled so line 4 crosses the map's wrap: the indices at x =
0 and 60, then 64 and 130 (a byte each).

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| T1 | `BGL0 T8` | F8FF / F0F8 | F8FF F8FF / F0F8 F0F8 | Layout 0, 8-pixel tiles: indices at x 0, 60 / 64, 130 |
| T2 | `BGL0 T16` | 0400 / F8FC | 0400 0400 / F8FC F8FC | Layout 0, 16-pixel tiles: indices at x 0, 60 / 64, 130 |
| T3 | `BGL0 T32` | 1612 / 080A | 1612 1612 / 080A 080A | Layout 0, 32-pixel tiles: indices at x 0, 60 / 64, 130 |
| T4 | `BGL0 T64` | 3736 / 2829 | 3736 3736 / 2829 2829 | Layout 0, 64-pixel tiles: indices at x 0, 60 / 64, 130 |
| T5 | `BGL1 T8` | F8FF / F0F8 | F8FF F8FF / F0F8 F0F8 | Layout 1, 8-pixel tiles: indices at x 0, 60 / 64, 130 |
| T6 | `BGL1 T16` | 0400 / F8FC | 0400 0400 / F8FC F8FC | Layout 1, 16-pixel tiles: indices at x 0, 60 / 64, 130 |
| T7 | `BGL1 T32` | 1612 / 080A | 1612 1612 / 080A 080A | Layout 1, 32-pixel tiles: indices at x 0, 60 / 64, 130 |
| T8 | `BGL1 T64` | 3736 / 2829 | 3736 3736 / 2829 2829 | Layout 1, 64-pixel tiles: indices at x 0, 60 / 64, 130 |
| T9 | `BGL2 T8` | F8FF / F0F8 | F8FF F8FF / F0F8 F0F8 | Layout 2, 8-pixel tiles: indices at x 0, 60 / 64, 130 |
| TA | `BGL2 T16` | 0400 / F8FC | 0400 0400 / F8FC F8FC | Layout 2, 16-pixel tiles: indices at x 0, 60 / 64, 130 |
| TB | `BGL2 T32` | 1612 / 080A | 1612 1612 / 080A 080A | Layout 2, 32-pixel tiles: indices at x 0, 60 / 64, 130 |
| TC | `BGL2 T64` | 3736 / 2829 | 3736 3736 / 2829 2829 | Layout 2, 64-pixel tiles: indices at x 0, 60 / 64, 130 |
| TD | `BGL3 T8` | F8FF / F0F8 | F8FF F8FF / F0F8 F0F8 | Layout 3, 8-pixel tiles: indices at x 0, 60 / 64, 130 |
| TE | `BGL3 T16` | 0400 / F8FC | 0400 0400 / F8FC F8FC | Layout 3, 16-pixel tiles: indices at x 0, 60 / 64, 130 |
| TF | `BGL3 T32` | 1612 / 080A | 1612 1612 / 080A 080A | Layout 3, 32-pixel tiles: indices at x 0, 60 / 64, 130 |
| TG | `BGL3 T64` | 3736 / 2829 | 3736 3736 / 2829 2829 | Layout 3, 64-pixel tiles: indices at x 0, 60 / 64, 130 |
| TH | `BGL4 T8` | F8FF / F0F8 | F8FF F8FF / F0F8 F0F8 | Layout 4, 8-pixel tiles: indices at x 0, 60 / 64, 130 |
| TI | `BGL4 T16` | 0400 / F8FC | 0400 0400 / F8FC F8FC | Layout 4, 16-pixel tiles: indices at x 0, 60 / 64, 130 |
| TJ | `BGL4 T32` | 1612 / 080A | 1612 1612 / 080A 080A | Layout 4, 32-pixel tiles: indices at x 0, 60 / 64, 130 |
| TK | `BGL4 T64` | 3736 / 2829 | 3736 3736 / 2829 2829 | Layout 4, 64-pixel tiles: indices at x 0, 60 / 64, 130 |
| TL | `BGL5 T8` | F8FF / F0F8 | F8FF F8FF / F0F8 F0F8 | Layout 5, 8-pixel tiles: indices at x 0, 60 / 64, 130 |
| TM | `BGL5 T16` | 0400 / F8FC | 0400 0400 / F8FC F8FC | Layout 5, 16-pixel tiles: indices at x 0, 60 / 64, 130 |
| TN | `BGL5 T32` | 1612 / 080A | 1612 1612 / 080A 080A | Layout 5, 32-pixel tiles: indices at x 0, 60 / 64, 130 |
| TO | `BGL5 T64` | 3736 / 2829 | 3736 3736 / 2829 2829 | Layout 5, 64-pixel tiles: indices at x 0, 60 / 64, 130 |
| TP | `BGL6 T8` | F8FF / F0F8 | F8FF F8FF / F0F8 F0F8 | Layout 6, 8-pixel tiles: indices at x 0, 60 / 64, 130 |
| TQ | `BGL6 T16` | 0400 / F8FC | 0400 0400 / F8FC F8FC | Layout 6, 16-pixel tiles: indices at x 0, 60 / 64, 130 |
| TR | `BGL6 T32` | 1612 / 080A | 1612 1612 / 080A 080A | Layout 6, 32-pixel tiles: indices at x 0, 60 / 64, 130 |
| TS | `BGL6 T64` | 3736 / 2829 | 3736 3736 / 2829 2829 | Layout 6, 64-pixel tiles: indices at x 0, 60 / 64, 130 |
| TT | `BGL7 T8` | F8FF / F0F8 | F8FF F8FF / F0F8 F0F8 | Layout 7, 8-pixel tiles: indices at x 0, 60 / 64, 130 |
| TU | `BGL7 T16` | 0400 / F8FC | 0400 0400 / F8FC F8FC | Layout 7, 16-pixel tiles: indices at x 0, 60 / 64, 130 |
| TV | `BGL7 T32` | 1612 / 080A | 1612 1612 / 080A 080A | Layout 7, 32-pixel tiles: indices at x 0, 60 / 64, 130 |
| TW | `BGL7 T64` | 3736 / 2829 | 3736 3736 / 2829 2829 | Layout 7, 64-pixel tiles: indices at x 0, 60 / 64, 130 |

### J: objects

One object at (16, 8) in each size (`SZ0-3`), then flipped in x and y and at 4bpp: the
indices on its first character row (x 16 and 24) and its second.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| J1 | `OBJ8 SZ0` | 2100 / 0000 | 2100 2100 / 0000 0000 | 8bpp object, size 0: indices on its first / second character row |
| J2 | `OBJ8 SZ1` | 2122 / 292A | 2122 2122 / 292A 292A | 8bpp object, size 1: indices on its first / second character row |
| J3 | `OBJ8 SZ2` | 2122 / 292A | 2122 2122 / 292A 292A | 8bpp object, size 2: indices on its first / second character row |
| J4 | `OBJ8 SZ3` | 2122 / 292A | 2122 2122 / 292A 292A | 8bpp object, size 3: indices on its first / second character row |
| J5 | `OBJ8 1 FX` | 2221 / 2A29 | 2221 2221 / 2A29 2A29 | 8bpp object, size 1, flipped in x: indices on its first / second character row |
| J6 | `OBJ8 1 FY` | 292A / 2122 | 292A 292A / 2122 2122 | 8bpp object, size 1, flipped in y: indices on its first / second character row |
| J7 | `OBJ4 SZ0` | 3100 / 0000 | 3100 3100 / 0000 0000 | 4bpp object, size 0: indices on its first / second character row |

## Part 4: when changes reach the picture

### L: scroll and palette writes at a line's start

A bitmap scroll or palette write at the start of line 49 or 50, line 50 captured: which of
them it shows (bitmap rows; the palette colour at x 0 and 200).

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| L1 | `SCRL 49/50` | 3232 / 3232 | 3232 3232 / 3232 3232 | Bitmap scroll written at line 49 / 50: the row line 50 shows |
| L2 | `PAL 49 L/R` | 7C00 / 7C00 | 7C00 7C00 / 7C00 7C00 | Palette written at line 49: colour at x 0 / 200 |
| L3 | `PAL 50 L/R` | 7C00 / 7C00 | 7C00 7C00 / 7C00 7C00 | Palette written at line 50: colour at x 0 / 200 |

### W: writes in the middle of a line

A palette (`PAL`), backdrop (`BKD`) or bitmap scroll X (`SCX`) write made once HCOUNT
reaches 0, 64, 128 or 192 of line 50: the first pixel of line 50 that shows it (999: none;
1000 + x: from x, but not to the end of the line), then HCOUNT read just before the write.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| W1 | `PAL H000` | 25.00 / 6.00 | 21 +5 / 2 +5 | Palette written at HCOUNT 0: first pixel showing it, HCOUNT before |
| W2 | `PAL H064` | 92.00 / 73.00 | 83 +11 / 64 +11 | Palette written at HCOUNT 64: first pixel showing it, HCOUNT before |
| W3 | `PAL H128` | 151.00 / 131.00 | 147 +11 / 128 +11 | Palette written at HCOUNT 128: first pixel showing it, HCOUNT before |
| W4 | `PAL H192` | 217.00 / 197.00 | 214 +6 / 195 +5 | Palette written at HCOUNT 192: first pixel showing it, HCOUNT before |
| W5 | `BKD H000` | 27.00 / 4.00 | 24 +6 / 1 +6 | Backdrop written at HCOUNT 0: first pixel showing it, HCOUNT before |
| W6 | `BKD H064` | 87.00 / 64.00 | 86 +10 / 64 +9 | Backdrop written at HCOUNT 64: first pixel showing it, HCOUNT before |
| W7 | `BKD H128` | 152.00 / 129.00 | 151 +10 / 128 +10 | Backdrop written at HCOUNT 128: first pixel showing it, HCOUNT before |
| W8 | `BKD H192` | 221.00 / 199.00 | 218 +6 / 196 +5 | Backdrop written at HCOUNT 192: first pixel showing it, HCOUNT before |
| W9 | `SCX H000` | 999.00 / 5.00 | 999 +0 / 2 +6 | Bitmap scroll X written at HCOUNT 0: first pixel showing it, HCOUNT before |
| WA | `SCX H064` | 999.00 / 70.00 | 999 +0 / 64 +10 | Bitmap scroll X written at HCOUNT 64: first pixel showing it, HCOUNT before |
| WB | `SCX H128` | 999.00 / 129.00 | 999 +0 / 128 +11 | Bitmap scroll X written at HCOUNT 128: first pixel showing it, HCOUNT before |
| WC | `SCX H192` | 999.00 / 200.00 | 999 +0 / 196 +5 | Bitmap scroll X written at HCOUNT 192: first pixel showing it, HCOUNT before |

### X: bitmap scroll X in horizontal blanking

Scroll X written in line 50's horizontal blank, at HCOUNT -70 to -12: the first pixel of
line 50 that shows it (0: the whole line, 999: none), then HCOUNT just before the write.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| X1 | `SCX HB-70` | 999.00 / -68.00 | 999 +0 / -69 +10 | Scroll X written at HCOUNT -70: first pixel showing it, HCOUNT before |
| X2 | `SCX HB-50` | 999.00 / -40.00 | 999 +0 / -48 +5 | Scroll X written at HCOUNT -50: first pixel showing it, HCOUNT before |
| X3 | `SCX HB-30` | 999.00 / -26.00 | 999 +0 / -22 +4 | Scroll X written at HCOUNT -30: first pixel showing it, HCOUNT before |
| X4 | `SCX HB-12` | 999.00 / -5.00 | 999 +0 / -10 +5 | Scroll X written at HCOUNT -12: first pixel showing it, HCOUNT before |

### F: flash write

The VDP's fill of a bitmap VRAM row: a full row, a masked one, and one started during the
picture: the first and last words.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| F1 | `FILL FULL` | 5A5A / 5A5A | 5A5A 5A5A / 5A5A 5A5A | A full row: first / last word |
| F2 | `FILL MASK` | 3535 / 3535 | 3535 3535 / 3535 3535 | A masked row |
| F3 | `FILL NOW` | 5A5A / 5A5A | 5A5A 5A5A / 5A5A 5A5A | Started during the picture |

### G: picture height

The first and last active lines of the 224- and 240-line pictures, and the four capture
formats at 240 lines.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| G1 | `224 R0/221` | 0000 / 00DD | 0000 0000 / 00DD 00DD | 224-line picture: line 0 / the last line (221) captured |
| G2 | `240 R0/237` | 0000 / 00ED | 0000 0000 / 00ED 00ED | 240-line picture: line 0 / the last line (237) captured |
| G3 | `240 F0 CRC` | 15CF / 7336 | 15CF 15CF / 7336 7336 | Capture format 0 at 240 lines: CRC-32 halves |
| G4 | `240 F1 CRC` | 588E / 10DE | 588E 588E / 10DE 10DE | Capture format 1 at 240 lines: CRC-32 halves |
| G5 | `240 F2 CRC` | DF4B / 16DF | DF4B DF4B / 16DF 16DF | Capture format 2 at 240 lines: CRC-32 halves |
| G6 | `240 F3 CRC` | DF4B / 16DF | DF4B DF4B / 16DF 16DF | Capture format 3 at 240 lines: CRC-32 halves |

## Part 5: snow

CPU accesses to tile VRAM, or reads of the palette, during the picture corrupt what a tile
layer draws. The corrupted pixels read 0000 (transparent, so the backdrop shows) whatever
the CPU wrote; a bitmap layer is not affected. A tile access blanks one or both 4-pixel
words of a tile about 8 pixels after HCOUNT; a palette read recolours the pixel being
drawn with the colour read.

### N and P: accesses kept up over a line

Line 100 captured while the CPU keeps writing tile VRAM (a spare character, the one shown,
or over a bitmap layer instead) or reading the palette (an unused entry, the one shown)
from line 99 to 101: the pixels that differ from an idle capture, over four frames, and in
how many frames any did.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| N1 | `IDLE BG` | 0.00 / 0.00 | 0 +0 / 0 +0 | IDLE BG: changed pixels over 4 frames, frames with any |
| N2 | `TILW SPARE` | 132.00 / 4.00 | 100 +72 / 4 +0 | TILW SPARE: changed pixels over 4 frames, frames with any |
| N3 | `TILW SHOWN` | 64.00 / 3.00 | 72 +68 / 4 +0 | TILW SHOWN: changed pixels over 4 frames, frames with any |
| N4 | `TILW BM` | 0.00 / 0.00 | 0 +0 / 0 +0 | TILW BM: changed pixels over 4 frames, frames with any |
| P1 | `PALR SPARE` | 38.00 / 4.00 | 38 +3 / 4 +0 | PALR SPARE: changed pixels over 4 frames, frames with any |
| P2 | `PALR SHOWN` | 0.00 / 0.00 | 0 +0 / 0 +0 | PALR SHOWN: changed pixels over 4 frames, frames with any |

### S and U: one access at a set point

One tile VRAM write (`TW5A` of 5A5A, `TW00` of 0000), tile VRAM read (`TRD`) or palette
read (`PRD`) once HCOUNT reaches 64-74 of line 100, six tries a spin loop's step apart: the
first changed pixel (999 none) and how many changed; then the first hit's colour and
HCOUNT.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| S1 | `TW5A T0` | 999.00 / 0.00 | 104 +895 / 0 +8 | Tile write of 5A5A, try 0: first changed pixel, how many |
| S2 | `TW5A T1` | 108.00 / 4.00 | 104 +895 / 0 +8 | Tile write of 5A5A, try 1: first changed pixel, how many |
| S3 | `TW5A T2` | 112.00 / 8.00 | 108 +891 / 0 +4 | Tile write of 5A5A, try 2: first changed pixel, how many |
| S4 | `TW5A T3` | 999.00 / 0.00 | 116 +883 / 0 +4 | Tile write of 5A5A, try 3: first changed pixel, how many |
| S5 | `TW5A T4` | 128.00 / 4.00 | 116 +883 / 0 +8 | Tile write of 5A5A, try 4: first changed pixel, how many |
| S6 | `TW5A T5` | 128.00 / 8.00 | 116 +883 / 0 +8 | Tile write of 5A5A, try 5: first changed pixel, how many |
| S7 | `TW5A HIT` | 0000 / 0044 | 0000 0000 / 0000 0051 | Tile write of 5A5A, first hit: colour, HCOUNT |
| S8 | `TW00 T0` | 999.00 / 0.00 | 104 +895 / 0 +8 | Tile write of 0000, try 0: first changed pixel, how many |
| S9 | `TW00 T1` | 108.00 / 4.00 | 104 +895 / 0 +8 | Tile write of 0000, try 1: first changed pixel, how many |
| SA | `TW00 T2` | 999.00 / 0.00 | 104 +895 / 0 +4 | Tile write of 0000, try 2: first changed pixel, how many |
| SB | `TW00 T3` | 999.00 / 0.00 | 116 +883 / 0 +4 | Tile write of 0000, try 3: first changed pixel, how many |
| SC | `TW00 T4` | 128.00 / 8.00 | 112 +887 / 0 +4 | Tile write of 0000, try 4: first changed pixel, how many |
| SD | `TW00 T5` | 128.00 / 8.00 | 112 +887 / 0 +4 | Tile write of 0000, try 5: first changed pixel, how many |
| SE | `TW00 HIT` | 0000 / 0046 | 0000 0000 / 0043 0050 | Tile write of 0000, first hit: colour, HCOUNT |
| U1 | `TRD T0` | 999.00 / 0.00 | 96 +903 / 0 +8 | Tile read, try 0: first changed pixel, how many |
| U2 | `TRD T1` | 999.00 / 0.00 | 96 +903 / 0 +8 | Tile read, try 1: first changed pixel, how many |
| U3 | `TRD T2` | 100.00 / 4.00 | 96 +903 / 0 +8 | Tile read, try 2: first changed pixel, how many |
| U4 | `TRD T3` | 999.00 / 0.00 | 108 +891 / 0 +4 | Tile read, try 3: first changed pixel, how many |
| U5 | `TRD T4` | 999.00 / 0.00 | 104 +895 / 0 +4 | Tile read, try 4: first changed pixel, how many |
| U6 | `TRD T5` | 116.00 / 4.00 | 104 +895 / 0 +4 | Tile read, try 5: first changed pixel, how many |
| U7 | `TRD HIT` | 0000 / 0046 | 0000 0000 / 0042 0050 | Tile read, first hit: colour, HCOUNT |
| U8 | `PRD T0` | 92.00 / 1.00 | 90 +909 / 0 +1 | Palette read, try 0: first changed pixel, how many |
| U9 | `PRD T1` | 999.00 / 0.00 | 90 +909 / 0 +1 | Palette read, try 1: first changed pixel, how many |
| UA | `PRD T2` | 111.00 / 1.00 | 92 +907 / 0 +1 | Palette read, try 2: first changed pixel, how many |
| UB | `PRD T3` | 114.00 / 1.00 | 101 +898 / 0 +1 | Palette read, try 3: first changed pixel, how many |
| UC | `PRD T4` | 115.00 / 1.00 | 102 +897 / 0 +1 | Palette read, try 4: first changed pixel, how many |
| UD | `PRD T5` | 116.00 / 1.00 | 100 +899 / 0 +1 | Palette read, try 5: first changed pixel, how many |
| UE | `PRD HIT` | 0000 / 0042 | 0000 0000 / 0042 0046 | Palette read, first hit: colour, HCOUNT |

### A, K and O: where in a line an access snows

One access at a time from on-chip RAM, two cycles a step across about HCOUNT 38-55 of
line 100: A tile VRAM writes, K tile VRAM reads, O palette reads. Each value is one try as
4 hex digits: the HCOUNT read right after the access (low 8 bits), then what changed. For
tile accesses the first changed pixel / 4 is in the low 6 bits and the number of changed
4-pixel groups less one in the top 2; for palette reads it is the first changed pixel;
`FF` means nothing changed. The console's pattern repeats every 8 pixels (one tile): the
map entry is fetched about 8 counts before the tile, its first word about 6 before and its
second about 3 before, with gaps where nothing snows.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| A1 | `TW N00` | 27FF / 28FF | 26FF 284C / 26FF 28FF | Tile write after 0 NOPs: two tries, HCOUNT and change |
| A2 | `TW N04` | 294C / 294C | 27FF 294C / 284C 2AFF | Tile write after 4 NOPs: two tries, HCOUNT and change |
| A3 | `TW N08` | 2BFF / 290C | 284C 2BFF / 294C 2CFF | Tile write after 8 NOPs: two tries, HCOUNT and change |
| A4 | `TW N0C` | 2BFF / 2BFF | 2A0C 2CFF / 2AFF 2D0D | Tile write after 12 NOPs: two tries, HCOUNT and change |
| A5 | `TW N10` | 2BFF / 2CFF | 2CFF 2E0D / 2CFF 2EFF | Tile write after 16 NOPs: two tries, HCOUNT and change |
| A6 | `TW N14` | 2D0D / 2FFF | 2CFF 2FFF / 2D0D 2FFF | Tile write after 20 NOPs: two tries, HCOUNT and change |
| A7 | `TW N18` | 2FFF / 304E | 2E0D 30FF / 2FFF 310E | Tile write after 24 NOPs: two tries, HCOUNT and change |
| A8 | `TW N1C` | 314E / 320E | 304E 314E / 304E 32FF | Tile write after 28 NOPs: two tries, HCOUNT and change |
| A9 | `TW N20` | 33FF / 33FF | 310E 33FF / 310E 34FF | Tile write after 32 NOPs: two tries, HCOUNT and change |
| AA | `TW N24` | 33FF / 34FF | 320E 34FF / 33FF 35FF | Tile write after 36 NOPs: two tries, HCOUNT and change |
| AB | `TW N28` | 350F / 360F | 33FF 360F / 34FF 36FF | Tile write after 40 NOPs: two tries, HCOUNT and change |
| AC | `TW N2C` | 350F / 37FF | 350F 37FF / 350F 3850 | Tile write after 44 NOPs: two tries, HCOUNT and change |
| K1 | `TR N00` | 27FF / 28FF | 27FF 294C / 27FF 2A4C | Tile read after 0 NOPs: two tries, HCOUNT and change |
| K2 | `TR N04` | 2A0C / 2A0C | 284C 2A4C / 284C 2BFF | Tile read after 4 NOPs: two tries, HCOUNT and change |
| K3 | `TR N08` | 2A0C / 2A0C | 294C 2CFF / 2A0C 2CFF | Tile read after 8 NOPs: two tries, HCOUNT and change |
| K4 | `TR N0C` | 2DFF / 2DFF | 2A0C 2DFF / 2BFF 2E0D | Tile read after 12 NOPs: two tries, HCOUNT and change |
| K5 | `TR N10` | 2CFF / 2CFF | 2CFF 2E0D / 2CFF 2FFF | Tile read after 16 NOPs: two tries, HCOUNT and change |
| K6 | `TR N14` | 2FFF / 2FFF | 2DFF 30FF / 2E0D 314E | Tile read after 20 NOPs: two tries, HCOUNT and change |
| K7 | `TR N18` | 314E / 314E | 2E0D 314E / 2FFF 324E | Tile read after 24 NOPs: two tries, HCOUNT and change |
| K8 | `TR N1C` | 30FF / 314E | 304E 330E / 314E 33FF | Tile read after 28 NOPs: two tries, HCOUNT and change |
| K9 | `TR N20` | 324E / 34FF | 314E 34FF / 320E 34FF | Tile read after 32 NOPs: two tries, HCOUNT and change |
| KA | `TR N24` | 35FF / 35FF | 320E 35FF / 33FF 360F | Tile read after 36 NOPs: two tries, HCOUNT and change |
| KB | `TR N28` | 35FF / 35FF | 34FF 370F / 35FF 37FF | Tile read after 40 NOPs: two tries, HCOUNT and change |
| KC | `TR N2C` | 360F / 360F | 350F 38FF / 360F 38FF | Tile read after 44 NOPs: two tries, HCOUNT and change |
| O1 | `PR N00` | 2827 / 29FF | 2624 28FF / 2625 2928 | Palette read after 0 NOPs: two tries, HCOUNT and change |
| O2 | `PR N04` | 2928 / 2726 | 2726 2928 / 2827 2A29 | Palette read after 4 NOPs: two tries, HCOUNT and change |
| O3 | `PR N08` | 2827 / 2B2A | 2928 2B29 / 2928 2BFF | Palette read after 8 NOPs: two tries, HCOUNT and change |
| O4 | `PR N0C` | 2A29 / 2DFF | 2928 2C2B / 2A29 2DFF | Palette read after 12 NOPs: two tries, HCOUNT and change |
| O5 | `PR N10` | 2D2C / 2D2C | 2B2A 2E2C / 2C2B 2EFF | Palette read after 16 NOPs: two tries, HCOUNT and change |
| O6 | `PR N14` | 2D2C / 2E2D | 2C2B 2F2E / 2D2C 2FFF | Palette read after 20 NOPs: two tries, HCOUNT and change |
| O7 | `PR N18` | 2F2E / 2F2E | 2D2C 302F / 2E2D 31FF | Palette read after 24 NOPs: two tries, HCOUNT and change |
| O8 | `PR N1C` | 3130 / 3130 | 2F2E 32FF / 302F 32FF | Palette read after 28 NOPs: two tries, HCOUNT and change |
| O9 | `PR N20` | 3332 / 3231 | 302F 33FF / 3130 33FF | Palette read after 32 NOPs: two tries, HCOUNT and change |
| OA | `PR N24` | 3332 / 3332 | 3130 34FF / 3231 3534 | Palette read after 36 NOPs: two tries, HCOUNT and change |
| OB | `PR N28` | 3332 / 3433 | 3332 3534 / 3432 3635 | Palette read after 40 NOPs: two tries, HCOUNT and change |
| OC | `PR N2C` | 3635 / 3635 | 3433 3735 / 3534 37FF | Palette read after 44 NOPs: two tries, HCOUNT and change |

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| SP | `SPURIOUS` | 0.00 / 0.00 | 0 +0 / 0 +0 | Interrupts nobody asked for (0) |

## The picture pages

After the result pages, for photos (nothing on them is measured):

1. Hi-res (blend mode 3) at 224 lines: fine red/green half-pixel stripes, A-only red
   columns, B-only green columns.
2. A white outline on the outermost active pixels of the 224-line picture.
3. The same for 240 lines.
4. Colour ramps: 32 steps of each channel.
5. Hi-res at 240 lines.
6. Tile VRAM written from line 8 to 200 every frame over a white block: tile-wide dark
   dashes, heavy enough to make the page hard to read.
7. The palette read the same way: a fainter speckle.

## The end of the list

The last line on the last result page is `CRC`: a CRC-32 of every result line.
