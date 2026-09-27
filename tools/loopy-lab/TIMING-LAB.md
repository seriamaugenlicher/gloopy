# Timing Lab: a timing test ROM for the Casio Loopy

`timing-lab.bin` measures a Casio Loopy's CPU, memory, interrupt, DMA and raster timing in
CPU cycles and shows the results on screen. The same ROM runs on a console and in an
emulator, so the two can be compared line by line. This document lists every test,
describes it, and gives the values a real console measured.

It comes in two builds with the same tests and IDs:

- **`timing-lab.bin`** runs every test once and shows named values, like `Q1 STRV ORM 131.17
  303.00`: for checking an emulator while developing it, and for regression tests.
- **`timing-lab_stress.bin`** runs the whole list **10 times** and shows, for each value, the
  lowest it gave and how far above that the highest was: for baselining hardware. The
  Loopy is not quite deterministic (DRAM refresh, the VDP's own memory cycles and where a
  test starts in the frame move some values), and the **10 runs** column below is that
  range. An emulator's value inside it matches the console.

The values come from one NTSC console, running the ROM from a flash cartridge with a pad
plugged in. Every result page was photographed and checked against the checksum printed on
it. The Loopy was only released in NTSC, so nothing here covers PAL.

The tables give each test two console values: **One run**, what a single run of the test
measured, and **10 runs**, the range the stress build gave on the same console. The video
tests (the VDP's own memories and what it draws) are in the companion `video-lab.bin`
(`VIDEO-LAB.md`).

## All tests


**Part 1: the controller port**

- **P: the controller port**
  - `P1` `BOOT IN0/1`: Pad inputs 0/1 as the BIOS leaves the port
  - `P2` `BOOT IN2/M`: Pad input 2 and VDP.MODE at boot
  - `P3` `DIR OUT00`: Direct mode, all outputs low
  - `P4` `DIR OUT01`: Direct mode, output 0 high
  - `P5` `DIR OUT02`: Direct mode, output 1 high
  - `P6` `DIR OUT04`: Direct mode, output 2 high
  - `P7` `DIR OUT08`: Direct mode, output 3 high
  - `P8` `DIR OUT10`: Direct mode, output 4 high
  - `P9` `DIR OUT20`: Direct mode, output 5 high
  - `PA` `DIR OUT3F`: Direct mode, all outputs high
  - `PB` `MTX IN0/1`: Matrix mode, pad inputs 0/1
  - `PC` `MTX IN2/MD`: Matrix mode, input 2 and VDP.MODE

**Part 2: the CPU**

- **F: instruction fetch and branches**
  - `F1` `NOP ROM`: NOP fetched from cartridge ROM
  - `F2` `NOP ROM +2`: NOP from ROM, loop at 4n+2
  - `F3` `NOP WRAM`: NOP fetched from work RAM
  - `F4` `NOP ORAM`: NOP fetched from on-chip RAM
  - `F5` `BRA+NOP ROM`: BRA with a delay slot, from ROM
  - `F6` `BRA+NOP ORM`: BRA with a delay slot, from on-chip RAM
  - `F7` `BT TAKN ROM`: Taken BT, from ROM
  - `F8` `BT TAKN ORM`: Taken BT, from on-chip RAM
  - `F9` `BF NOT ROM`: Untaken BF, from ROM
  - `FA` `BSR+RTS ROM`: BSR/RTS call and return, from ROM
  - `FB` `JSR+RTS ROM`: JSR/RTS call and return, from ROM
  - `FC` `LIT ROM`: PC-relative literal read from ROM
- **S: an access right before a branch**
  - `S1` `LD+BRA`: Work RAM read before BRA
  - `S2` `ST+BRA`: Work RAM write before BRA
  - `S3` `LDX+BRA`: Row-changing read before BRA
  - `S4` `LD+BSR`: Read before BSR
  - `S5` `LD+RTS`: Read before RTS
  - `S6` `LD+JSR`: Read before JSR
- **M: multi-cycle and system register instructions**
  - `M1` `MULS ORM`: MULS.W throughput
  - `M2` `MUL+STS ORM`: MULS.W result latency
  - `M3` `DIV1 ORM`: DIV1
  - `M4` `TAS.B ORM`: TAS.B read-modify-write
  - `M5` `MACW ORM`: MAC.W throughput
  - `M6` `MACW+STS`: MAC.W result latency
  - `M7` `GBR L ROM`: STC.L/LDC.L GBR on work RAM, from ROM
  - `M8` `GBR L ORM`: STC.L/LDC.L GBR on on-chip RAM
  - `M9` `PR L ROM`: STS.L/LDS.L PR on work RAM

**Part 3: memory**

- **W: work RAM**
  - `W1` `LDW ROW ROM`: Work RAM word read, same row
  - `W2` `LDL ROW ROM`: Work RAM longword read, same row
  - `W3` `STW ROW ROM`: Work RAM word write, same row
  - `W4` `STL ROW ROM`: Work RAM longword write, same row
  - `W5` `LDW ALT ROM`: Word reads alternating DRAM rows
  - `W6` `STW ALT ROM`: Word writes alternating DRAM rows
  - `W7` `LDW ROW ORM`: Work RAM word read from on-chip RAM code
  - `W8` `LU DEP ROM`: Load-use stall, from ROM
  - `W9` `LU IND ROM`: Load then independent op, from ROM
  - `WA` `LU DEP ORM`: Load-use stall, from on-chip RAM
  - `WB` `LU IND ORM`: Load then independent op, from on-chip RAM
  - `WC` `RD>WR ROM`: Read then write, same row
  - `WD` `WR>WR ROM`: Write then write, same row
  - `WE` `WR>RD ROM`: Write then read, same row
  - `WF` `GAP S/A`: Read after an idle gap: row kept or closed
  - `WG` `GAP A0/A2`: Read after a gap at 4n / 4n+2
  - `WH` `RF STL/MAX`: DRAM refresh stall, mean and largest
  - `WI` `RF PER/WIN`: DRAM refresh period
- **N: the NOP-loop test ROM's loop, built up access by access**
  - `N1` `NOPLOOP`: The NOP-loop test ROM's loop
  - `N2` `NOPLOOP -ST`: That loop without its store
  - `N3` `NOPLOOP -LD`: That loop without its loads
  - `N4` `NL SKEL`: That loop's skeleton alone
  - `N5` `NL LD1`: Skeleton plus the first load
  - `N6` `NL LD2`: Skeleton plus the second load
  - `N7` `NL ST+LD1`: Skeleton, store and first load
  - `N8` `NL ST+LD2`: Skeleton, store and second load
  - `N9` `NL ST R2`: Whole loop, loads into r2
- **O: on-chip RAM as data**
  - `O1` `OR RDW ROM`: On-chip RAM word read, from ROM
  - `O2` `OR WRW ROM`: On-chip RAM word write, from ROM
  - `O3` `OR RDL ROM`: On-chip RAM longword read, from ROM
  - `O4` `OR WRL ROM`: On-chip RAM longword write, from ROM
  - `O5` `OR RDB ROM`: On-chip RAM byte read, from ROM
  - `O6` `OR WRB ROM`: On-chip RAM byte write, from ROM
  - `O7` `OR RDW ORM`: On-chip RAM word read, from itself
  - `O8` `OR WRW ORM`: On-chip RAM word write, from itself
  - `O9` `OR RDL ORM`: On-chip RAM longword read, from itself
  - `OA` `OR WRL ORM`: On-chip RAM longword write, from itself
- **C: cartridge SRAM**
  - `C1` `SRM RD ROM`: Cartridge SRAM byte read
- **K: open bus and mirrors**
  - `K1` `OPEN BUS`: What unmapped VDP reads return
  - `K2` `WO TRG/SYN`: Write-only VDP registers read back
  - `K3` `VDP +1/+3M`: VDP mirrors at +1 / +3 MB
  - `K4` `VDP LO 8B`: VDP through the 8-bit area-4 mirror
  - `K5` `WRAM MR/A1`: Work RAM mirror and area-1 byte lanes
  - `K6` `ROM +1/+2M`: Cartridge ROM mirrors (flash cart)
  - `K7` `SRAM8K/32K`: Cartridge SRAM mirrors (flash cart)
  - `K8` `ORAM 1K/4K`: On-chip RAM mirrors

**Part 4: exceptions and interrupts**

- **T: TRAPA**
  - `T1` `TRAPA+RTE`: TRAPA and RTE round trip
  - `T2` `TRAPA I/O`: TRAPA entry and exit separately
  - `T3` `ST+TRAPA`: TRAPA after a store
  - `T4` `LD+TRAPA`: TRAPA after a load
- **L: the ITU and interrupt latency**
  - `L1` `ITUR 00/01`: ITU interrupt latency, ROM loop, phase 0-1
  - `L2` `ITUR 02/03`: ITU latency, ROM loop, phase 2-3
  - `L3` `ITUR 04/05`: ITU latency, ROM loop, phase 4-5
  - `L4` `ITUR 06/07`: ITU latency, ROM loop, phase 6-7
  - `L5` `ITUR 08/09`: ITU latency, ROM loop, phase 8-9
  - `L6` `ITUR 10/11`: ITU latency, ROM loop, phase 10-11
  - `L7` `ITUR 12/13`: ITU latency, ROM loop, phase 12-13
  - `L8` `ITUR 14/15`: ITU latency, ROM loop, phase 14-15
  - `L9` `ITUO 00/01`: ITU latency, on-chip RAM loop, phase 0-1
  - `LA` `ITUO 02/03`: ITU latency, on-chip RAM loop, phase 2-3
  - `LB` `ITUO 04/05`: ITU latency, on-chip RAM loop, phase 4-5
  - `LC` `ITUO 06/07`: ITU latency, on-chip RAM loop, phase 6-7
  - `LD` `ITU ROM/OR`: ITU latency at one phase, ROM / on-chip RAM
  - `LE` `SLEEP ITU`: ITU latency from SLEEP
  - `LF` `ITUFLG R/O`: ITU compare flag polled, no interrupt
  - `LG` `TSTR R/O`: TCNT just after the timer starts
  - `LH` `TCNT2 R/O`: Two TCNT reads back to back
- **I: where interrupts land**
  - `I1` `IRQ0 -80`: IRQ0 position, compare at HCOUNT -80
  - `I2` `IRQ0 200`: IRQ0 position, compare at HCOUNT 200
  - `I3` `NMI V/H`: Vblank NMI position
  - `I4` `IRQ1 FRAME`: IRQ1 position, frame mode
  - `I5` `IRQ1 LINE`: IRQ1 position, line mode
  - `I6` `STORM A/V`: Level IRQ1 storm: where interrupts land
  - `I7` `STRV N/PRG`: Level storm starves the program
  - `I8` `I0 NO PA12`: IRQ0 without its PA12 pin
- **R: interrupt round trips under a level-sense storm**
  - `R1` `BARE ORM`: Storm round trip, bare handler
  - `R2` `BARE ROM`: Round trip, program in ROM
  - `R3` `ORM STCK`: Round trip, stack in on-chip RAM
  - `R4` `ORM HNDL`: Round trip, handler in on-chip RAM
  - `R5` `ORM ALL`: Round trip, all in on-chip RAM
  - `R6` `ROM OHNDL`: Round trip, ROM program, on-chip handler
  - `R7` `PUSH3`: Round trip, handler saving 3 registers
  - `R8` `R3 HNDL`: Round trip, the split-count handler
  - `R9` `STRV HNDL`: Round trip, the starve handler

**Part 5: DMA**

- **D: DMA**
  - `D1` `WW BST`: DMA work RAM to work RAM, burst
  - `D2` `WW CST`: DMA work RAM to work RAM, cycle steal
  - `D3` `RW BST`: DMA ROM to work RAM, burst
  - `D4` `WW B8`: DMA byte units, burst
  - `D5` `START B/C`: DMA start-up time
  - `D6` `POLLS B/C`: CPU progress during DMA
- **X: raster DMA on DREQ0**
  - `X1` `LN N/V1`: Raster DMA, line mode: count, first line
  - `X2` `LN V2/VL`: Raster DMA, line mode: lines
  - `X3` `LN H1/H2`: Raster DMA, line mode: HCOUNT
  - `X4` `FR N/V`: Raster DMA, frame mode: count, line
  - `X5` `FR H`: Raster DMA, frame mode: HCOUNT
  - `X6` `LV N/RATE`: Raster DMA, level sense: rate
  - `X7` `LV V1/VL`: Raster DMA, level sense: lines

**Part 6: the picture's timing**

- **G: lines, frames, the raster signal and the 240-line picture**
  - `G1` `LINE/FRM K`: Cycles per line and per frame
  - `G2` `VSTEP H/RT`: HCOUNT at the line step, and its rate
  - `G3` `FR FALL VH`: Raster signal fall, frame mode
  - `G4` `FR RISE VH`: Raster signal rise, frame mode
  - `G5` `LN FALL VH`: Raster signal fall, line mode
  - `G6` `LN RISE VH`: Raster signal rise, line mode
  - `G7` `240 NMI VH`: 240 lines: NMI position
  - `G8` `240 V RNG`: 240 lines: VCOUNT range
  - `G9` `240 FR FAL`: 240 lines: raster signal fall
  - `GA` `240 FR RIS`: 240 lines: raster signal rise
  - `GB` `240 STRV N`: 240 lines: the starve storm

**Part 7: the ADC**

- **A: the ADC and IRQ2**
  - `A1` `ADC CH0/1`: ADC channels 0 and 1
  - `A2` `ADC CH2/3`: ADC channels 2 and 3
  - `A3` `ADC CH4/LT`: ADC channel 4 and conversion time
  - `A4` `CAPIRQ2 VH`: Capture-ready IRQ2 position

**Part 8: the starve handler taken apart**

- **Z: the handler's body in pieces**
  - `Z1` `HS VDP RD2`: Storm handler: literal and two VDP reads
  - `Z2` `HS ST MV ST`: Storm handler: store, move, store
  - `Z3` `HS ST ST`: Storm handler: two stores
  - `Z4` `HS LD TST`: Storm handler: load and TST
  - `Z5` `HS LD T BF`: Storm handler: load, TST, BF taken
  - `Z6` `HS BODY`: Storm handler: the whole body
- **Y: its entry and exit as straight-line code**
  - `Y1` `HE LIT LD2`: Two literal loads
  - `Y2` `HE PSH3 POP`: Three pushes and pops
  - `Y3` `HE ENTRY`: The handler's start
  - `Y4` `HE ALL`: The handler as straight-line code
- **Q: storm round trips with one thing changed**
  - `Q1` `STRV ORM`: Storm round trip, program in on-chip RAM
  - `Q2` `STRV ROM`: Storm round trip, program in ROM
  - `Q3` `STRV OSTK`: Storm round trip, stack in on-chip RAM
  - `Q4` `STRV OHND`: Storm round trip, handler in on-chip RAM
  - `Q5` `NOVDP ORM`: No VDP reads, program in on-chip RAM
  - `Q6` `NOVDP ROM`: No VDP reads, program in ROM
  - `Q7` `PAD ORM`: 16 NOPs more, program in on-chip RAM
  - `Q8` `PAD ROM`: 16 NOPs more, program in ROM
  - `Q9` `R3H ORM`: Split-count handler, program in on-chip RAM
  - `QA` `R3H ROM`: Split-count handler, program in ROM
  - `QB` `PUSH3 ROM`: Push3 handler, program in ROM
  - `QC` `LONG3 ORM`: Long plain handler, program in on-chip RAM
  - `QD` `LONG3 ROM`: Long plain handler, program in ROM

**Part 9: DRAM refresh**

- **U: what a refresh costs, by what the bus did meanwhile**
  - `U1` `RD ORM MIN`: Refresh walk: read, idle on-chip RAM, shortest
  - `U2` `RD ORM H0`: ... passes +0-3
  - `U3` `RD ORM H4`: ... passes +4-7
  - `U4` `WR ORM MIN`: Write, idle on-chip RAM, shortest
  - `U5` `WR ORM H0`: ... passes +0-3
  - `U6` `WR ORM H4`: ... passes +4-7
  - `U7` `RD ROM MIN`: Read, idle in ROM, shortest
  - `U8` `RD ROM H0`: ... passes +0-3
  - `U9` `RD ROM H4`: ... passes +4-7
  - `UA` `WR ROM MIN`: Write, idle in ROM, shortest
  - `UB` `WR ROM H0`: ... passes +0-3
  - `UC` `WR ROM H4`: ... passes +4-7
  - `UD` `RD VDP MIN`: Read, idle VDP reads, shortest
  - `UE` `RD VDP H0`: ... passes +0-3
  - `UF` `RD VDP H4`: ... passes +4-7
  - `UG` `WR VDP MIN`: Write, idle VDP reads, shortest
  - `UH` `WR VDP H0`: ... passes +0-3
  - `UI` `WR VDP H4`: ... passes +4-7

**Part 10: misaligned accesses**

- **E: misaligned accesses**
  - `E1` `AE RDB ODD`: Odd byte read (no error)
  - `E2` `AE RDW ODD`: Odd word read address error
  - `E3` `AE RDL +2`: Misaligned longword read error
  - `E4` `AE WRW ODD`: Odd word write error
  - `E5` `AE WRL +2`: Misaligned longword write error
  - `E6` `AE WR MEM`: Where misaligned writes land
  - `E7` `AE RDW ORM`: Odd on-chip RAM read error
  - `E8` `AE FETCH`: Jump to an odd address
- `SP` `SPURIOUS`: Unrequested interrupts

## Running it

- **On a console:** flash `timing-lab.bin` (or `timing-lab_stress.bin`) to a cartridge. Plug a pad in and press nothing
  until the result pages appear: about 15 seconds, or 100 seconds for the stress build (whose header shows `RUN nn/10` while it measures). **D-pad left and right turn the pages.** Until the first press they turn by
  themselves every 6 seconds. Each page ends with a `PAGE CRC`, a CRC-32 of that page's
  lines, so a photo or a transcription can be checked on its own. **Nothing is written to
  cartridge SRAM**, so it runs on a flash cart whose SRAM battery is out.
- **In an emulator:** run it for 900 frames (6000 for the stress build). The results are at work RAM `0x09070000`
  ("TLAB", the line count's low byte, the width, its high byte, a zero, then the screen
  text).

## Reading a line

In the plain build each line is `ID NAME value value`, the ID being the section's letter and a
running number. In the stress build it is `ID lo +d lo +d`: the ID, then for each of the line's two values the lowest the 10 runs gave and how much
higher the highest was (`+0` when every run agreed). Hex readings show their lowest and
highest instead. The tables below give each test's name. What the two values are:

In the plain build a mark left of each ID judges the line against the console's 10-run
range: a green `O` when every value lies inside it, a red `X` when one does not. Hex readings
are judged as whole values and ADC readings within 8 counts; a median's spread is not judged.
The flash cart's lines get no mark, and neither do the refresh walk's histograms (section
U's `H0` and `H4` lines): each byte there is a separate bin, and the stress build keeps only
each word's lowest and highest, not each bin's. The marks are drawn on screen only, so the
page checksums do not cover them.

- **Cycle counts:** the median of 7 repeats, in CPU cycles at the 16 MHz CPU clock (the
  ITU counter runs at the full clock). The second value is either:
  - the spread between the fastest and slowest of the 7 repeats (the tables show it as
    `(spread s)` where it is not zero);
  - a second median.
- **Hex lines:** two 16-bit register or memory readings.
- **HCOUNT and VCOUNT:** shown signed, with blanking negative.
  - HCOUNT runs -84 to 257 per line, one count every 4 VDP clocks, and the line starts at
    HCOUNT -84.
  - VCOUNT runs -39 to 223 in the 224-line picture and -23 to 239 in the 240-line one.
- **The BOOT lines** read the port as the BIOS leaves it, so only the first run counts.

Code places:

- `ROM`: cartridge ROM, 16-bit, on the external bus.
- `WRAM`: work RAM, 16-bit DRAM.
- `ORM` / `ORAM`: the CPU's on-chip RAM, 32 bits wide on the internal bus.

Each kernel is timed against an empty run from the same place and divided by the operations
it contains.

### How much a value can move

The **10 runs** column is the tolerance the hardware itself shows: a value inside it
matches the console, whatever the scoring below says. What moves most:

- **Positions** (HCOUNT/VCOUNT where something lands) move by a count or two.
- **ADC readings** are analog, and the unconnected channels drift between power-ups.
- **The frame length** (G1) moves by under a cycle a line.
- **The refresh walk** (section U) is a histogram of where refreshes happened to land.

**Suggested scoring** (not part of the ROM):

- **EXACT:** identical.
- **PASS:** within 0.10 or 3% of the console, whichever is looser, or inside the 10-run
  range.
- **FAIL:** anything else.
- **Hex readings** must be exact, with two exceptions: ADC readings always pass (they are
  analog and differ between consoles),
  and each bin of a refresh-walk histogram (section U's `H0` / `H4` lines, a byte each)
  passes within 4 counts of the console's, about what the bins move between runs.
- **Not scored:** the two lines marked as the cartridge's describe the flash cart, not the
  console.

## Part 1: the controller port

### P: the controller port

Read first, before anything else touches the port. The BIOS leaves it in **direct** mode
(VDP.MODE CMODE = 0), not matrix mode. The section then drives each output alone, then all
low and all high, then switches to matrix mode.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| P1 | `BOOT IN0/1` | 0000 / 0000 | 0000 0000 / 0000 0000 | CONTROL_IN[0] / [1] as the BIOS leaves the port (direct mode), pad plugged, nothing pressed (hex) |
| P2 | `BOOT IN2/M` | 0000 / 0020 | 0000 0000 / 0020 0020 | CONTROL_IN[2] and VDP.MODE as the BIOS leaves them (hex) |
| P3 | `DIR OUT00` | 0000 / 0000 | 0000 0000 / 0000 0000 | Direct mode, CONTROL_OUT = 0x00, 2000 cycles later: CONTROL_IN[0] / [1] (hex) |
| P4 | `DIR OUT01` | 0001 / 0000 | 0001 0001 / 0000 0000 | The same with output 0 alone high |
| P5 | `DIR OUT02` | 0001 / 0000 | 0001 0001 / 0000 0000 | Output 1 alone high |
| P6 | `DIR OUT04` | 0000 / 0000 | 0000 0000 / 0000 0000 | Output 2 alone high |
| P7 | `DIR OUT08` | 0000 / 0000 | 0000 0000 / 0000 0000 | Output 3 alone high |
| P8 | `DIR OUT10` | 0000 / 0000 | 0000 0000 / 0000 0000 | Output 4 alone high |
| P9 | `DIR OUT20` | 0000 / 0000 | 0000 0000 / 0000 0000 | Output 5 alone high |
| PA | `DIR OUT3F` | 0101 / 0000 | 0001 0001 / 0000 0000 | All six outputs high, straight after DIR OUT20 (output 5 has then been high for over 4000 cycles; the high byte's bit needs more than 2000) |
| PB | `MTX IN0/1` | 0001 / 0000 | 0001 0001 / 0000 0000 | Matrix mode, two frames later: CONTROL_IN[0] / [1] (hex; bit 0 = pad present) |
| PC | `MTX IN2/MD` | 0000 / 0030 | 0000 0000 / 0030 0030 | Matrix mode: CONTROL_IN[2] and VDP.MODE (hex) |

## Part 2: the CPU

### F: instruction fetch and branches

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| F1 | `NOP ROM` | 3.00 | 3 +0 | NOP from cartridge ROM, loop head at 4n |
| F2 | `NOP ROM +2` | 3.00 | 3 +0 | NOP from cartridge ROM, loop head at 4n+2 |
| F3 | `NOP WRAM` | 1.02 | 1.02 +0 | NOP from work RAM |
| F4 | `NOP ORAM` | 1.00 | 1 +0 | NOP from on-chip RAM |
| F5 | `BRA+NOP ROM` | 7.00 | 7 +0 | BRA to the next instruction with a NOP in its delay slot, from ROM |
| F6 | `BRA+NOP ORM` | 3.00 | 3 +0 | The same from on-chip RAM |
| F7 | `BT TAKN ROM` | 9.00 | 9 +0 | BT taken to the next instruction, from ROM |
| F8 | `BT TAKN ORM` | 3.00 | 3 +0 | The same from on-chip RAM |
| F9 | `BF NOT ROM` | 3.00 | 3 +0 | BF not taken, from ROM |
| FA | `BSR+RTS ROM` | 14.01 | 14.01 +0 | BSR to an RTS and back, both with NOP slots, from ROM |
| FB | `JSR+RTS ROM` | 14.02 | 14.02 +0 | JSR to an RTS and back, from ROM |
| FC | `LIT ROM` | 9.00 | 9 +0 | MOV.L @(disp,PC): a longword literal read from cartridge ROM |

### S: an access right before a branch

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| S1 | `LD+BRA` | 10.13 (spread +0.01) | 10.13 +0 | A work RAM read right before BRA (per pair, from ROM) |
| S2 | `ST+BRA` | 11.08 (spread +0.01) | 11.08 +0 | A work RAM write right before BRA |
| S3 | `LDX+BRA` | 12.02 | 12.02 +0 | A row-changing read right before BRA |
| S4 | `LD+BSR` | 17.16 (spread +0.01) | 17.16 +0.01 | A read right before BSR (with the RTS back) |
| S5 | `LD+RTS` | 17.17 (spread +0.01) | 17.17 +0 | A read right before RTS |
| S6 | `LD+JSR` | 17.17 | 17.17 +0 | A read right before JSR |

### M: multi-cycle and system register instructions

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| M1 | `MULS ORM` | 2.94 | 2.94 +0 | MULS.W back to back, from on-chip RAM |
| M2 | `MUL+STS ORM` | 5.00 | 5 +0 | MULS.W then STS MACL (result latency) |
| M3 | `DIV1 ORM` | 1.00 | 1 +0 | DIV1 |
| M4 | `TAS.B ORM` | 5.65 (spread +0.01) | 5.65 +0 | TAS.B on work RAM, from on-chip RAM |
| M5 | `MACW ORM` | 2.93 | 2.93 +0.01 | MAC.W back to back, operands in on-chip RAM |
| M6 | `MACW+STS` | 5.00 | 5 +0 | MAC.W then STS MACL |
| M7 | `GBR L ROM` | 13.09 (spread +0.01) | 13.09 +0 | STC.L GBR / LDC.L GBR pair on work RAM, from ROM |
| M8 | `GBR L ORM` | 6.00 | 6 +0 | The same on on-chip RAM, from on-chip RAM |
| M9 | `PR L ROM` | 11.15 (spread +0.01) | 11.15 +0 | STS.L PR / LDS.L PR pair on work RAM, from ROM |

## Part 3: memory

### W: work RAM

Work RAM is 16-bit DRAM with 1 KB rows. It pays 3 cycles on a row change and 1 per
transfer within the open row. A CBR refresh comes every 244 cycles and closes the row.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| W1 | `LDW ROW ROM` | 4.06 | 4.06 +0 | Work RAM word read, all in one DRAM row, from ROM code |
| W2 | `LDL ROW ROM` | 5.09 | 5.09 +0 | Work RAM longword read, one row |
| W3 | `STW ROW ROM` | 5.08 | 5.08 +0 | Work RAM word write, one row |
| W4 | `STL ROW ROM` | 6.10 | 6.10 +0 | Work RAM longword write, one row |
| W5 | `LDW ALT ROM` | 6.03 | 6.03 +0 | Work RAM word reads alternating between two rows |
| W6 | `STW ALT ROM` | 6.03 | 6.03 +0 | Word writes alternating between two rows |
| W7 | `LDW ROW ORM` | 1.54 (spread +0.01) | 1.54 +0 | Work RAM word read, one row, from on-chip RAM code |
| W8 | `LU DEP ROM` | 7.09 | 7.09 +0.01 | A work RAM load, then an instruction using its register, from ROM |
| W9 | `LU IND ROM` | 7.10 | 7.10 +0 | A load, then an instruction not using it, from ROM |
| WA | `LU DEP ORM` | 3.05 | 3.05 +0 | Load-use from on-chip RAM (the dependent pair costs a cycle more) |
| WB | `LU IND ORM` | 2.05 (spread +0.01) | 2.04 +0.01 | Load then independent, from on-chip RAM |
| WC | `RD>WR ROM` | 9.11 (spread +0.01) | 9.11 +0 | Work RAM word read then write in one row, per pair |
| WD | `WR>WR ROM` | 10.17 | 10.17 +0 | Write then write in one row |
| WE | `WR>RD ROM` | 9.11 (spread +0.01) | 9.11 +0 | Write then read in one row |
| WF | `GAP S/A` | 3.52 / 3.50 | 3.50 +0.02 / 3.50 +0.02 | A read after a ~500-cycle idle gap, over the gap alone: same row as before / another row |
| WG | `GAP A0/A2` | 2.00 / 3.02 | 2 +0.03 / 3 +0.02 | The same-row case with the read at 4n / 4n+2 (from on-chip RAM) |
| WH | `RF STL/MAX` | 0.00 / 0.00 | 0 +0 / 0 +0 | Refresh stall: mean and largest extra cycles in a short window that caught a refresh (hundredths) |
| WI | `RF PER/WIN` | 0.00 / 54.00 | 0 +0 / 54 +0 | Refresh period estimate, and the window length it came from |

### N: the NOP-loop test ROM's loop, built up access by access

A widely used hardware test ROM's inner loop, instruction for instruction, then the same
loop with its store and loads added back one at a time.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| N1 | `NOPLOOP` | 37.61 (spread +0.02) | 37.61 +0 | The NOP-loop test ROM's inner loop from ROM, cycles per iteration |
| N2 | `NOPLOOP -ST` | 35.39 (spread +0.01) | 35.38 +0.01 | The same loop without its store |
| N3 | `NOPLOOP -LD` | 36.22 | 36.22 +0 | Without its two loads |
| N4 | `NL SKEL` | 34.07 | 34.07 +0 | The loop with nothing in the store and load slots |
| N5 | `NL LD1` | 35.38 (spread +0.01) | 35.38 +0 | Only the first load |
| N6 | `NL LD2` | 34.38 | 34.38 +0 | Only the second load |
| N7 | `NL ST+LD1` | 37.29 | 37.29 +0 | The store and the first load |
| N8 | `NL ST+LD2` | 36.41 (spread +0.01) | 36.41 +0 | The store and the second load |
| N9 | `NL ST R2` | 37.61 (spread +0.01) | 37.61 +0 | The whole loop with its loads into r2 instead of r0 |

### O: on-chip RAM as data

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| O1 | `OR RDW ROM` | 4.00 | 4 +0 | On-chip RAM word read from ROM code |
| O2 | `OR WRW ROM` | 4.00 | 4 +0 | On-chip RAM word write from ROM code |
| O3 | `OR RDL ROM` | 4.00 | 4 +0 | Longword read from ROM code |
| O4 | `OR WRL ROM` | 4.00 | 4 +0 | Longword write from ROM code |
| O5 | `OR RDB ROM` | 4.00 | 4 +0 | Byte read from ROM code |
| O6 | `OR WRB ROM` | 4.00 | 4 +0 | Byte write from ROM code |
| O7 | `OR RDW ORM` | 1.50 | 1.50 +0 | Word read from on-chip RAM code |
| O8 | `OR WRW ORM` | 1.50 | 1.50 +0 | Word write from on-chip RAM code |
| O9 | `OR RDL ORM` | 1.50 | 1.50 +0 | Longword read from on-chip RAM code |
| OA | `OR WRL ORM` | 1.50 | 1.50 +0 | Longword write from on-chip RAM code |

### C: cartridge SRAM

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| C1 | `SRM RD ROM` | 6.00 | 6 +0 | Cartridge SRAM byte read from ROM code |

### K: open bus and mirrors

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| K1 | `OPEN BUS` | 0000 / 5A3C | 0000 0000 / 5A3C 5A3C | Unmapped VDP reads after a palette write / after a tile VRAM read (hex) |
| K2 | `WO TRG/SYN` | 5A3C / 25C3 | 5A3C 5A3C / 25C3 25C3 | Write-only TRIGGER and SYNC_CALIBRATE read back (hex) |
| K3 | `VDP +1/+3M` | 25C3 / 25C3 | 25C3 25C3 / 25C3 25C3 | The palette through the VDP's +1 MB and +3 MB mirrors (hex) |
| K4 | `VDP LO 8B` | 00C3 / 00C3 | 00C3 00C3 / 00C3 00C3 | A palette word through the 8-bit area-4 mirror (hex) |
| K5 | `WRAM MR/A1` | 1357 / 5757 | 1357 1357 / 5757 5757 | Work RAM through its +512 KB mirror / through area 1 (byte lanes) (hex) |
| K6 | `ROM +1/+2M` | 0001 / FFFF (cart, not scored) | 0001 0001 / FFFF FFFF | Cartridge ROM at +1 / +2 MB (the flash cart's, not the console's) |
| K7 | `SRAM8K/32K` | F293 / 2EB6 (cart, not scored) | E293 E293 / 2EB6 2EB6 | Cartridge SRAM at +8 / +32 KB (the flash cart's; nothing is written first, so it shows whatever SRAM holds) |
| K8 | `ORAM 1K/4K` | 2468 / 2468 | 2468 2468 / 2468 2468 | On-chip RAM at +1 / +4 KB (hex) |

## Part 4: exceptions and interrupts

### T: TRAPA

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| T1 | `TRAPA+RTE` | 34.61 (spread +0.04) | 34.61 +0 | TRAPA to a handler that only returns, from ROM |
| T2 | `TRAPA I/O` | 53.00 / 39.00 | 53 +0 / 39 +0 | TRAPA: cycles from before it to the handler's first TCNT read / from the handler's last read to after the RTE |
| T3 | `ST+TRAPA` | 39.52 (spread +0.05) | 39.52 +0 | TRAPA+RTE after a store in the stack's DRAM row |
| T4 | `LD+TRAPA` | 38.60 (spread +0.08) | 38.58 +0.02 | After a load in the stack's row |

### L: the ITU and interrupt latency

The ITU0 compare-match interrupt, with the compare value stepped one cycle at a time. That
walks the moment of the match through every point of the CPU's spin loop: 16 cycles from
ROM, 6 from on-chip RAM. So the first twelve lines show how long a request takes at each
point in the loop, not a single median. `ITU ROM/OR` and `SLEEP ITU` are the same
measurement at one compare value, spinning and asleep. The last lines take the interrupt
path out, to separate the timer from the CPU's acceptance:

- the flag polled instead of taken;
- when TCNT starts counting after the TSTR write;
- what two back-to-back reads of it see.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| L1 | `ITUR 00/01` | 56.00 / 55.00 | 56 +0 / 55 +0 | ITU0 compare match A at 3000 + k (k = 0, 1) to the handler's TCNT read, minus the compare value; CPU spinning in ROM |
| L2 | `ITUR 02/03` | 54.00 / 54.00 | 54 +0 / 53 +0 | The same, k = 2, 3 |
| L3 | `ITUR 04/05` | 61.00 / 60.00 | 61 +0 / 60 +0 | k = 4, 5 |
| L4 | `ITUR 06/07` | 59.00 / 59.00 | 59 +0 / 58 +0 | k = 6, 7 |
| L5 | `ITUR 08/09` | 57.00 / 56.00 | 57 +0 / 56 +0 | k = 8, 9 |
| L6 | `ITUR 10/11` | 55.00 / 55.00 | 55 +0 / 54 +0 | k = 10, 11 |
| L7 | `ITUR 12/13` | 53.00 / 55.00 | 53 +0 / 55 +0 | k = 12, 13 |
| L8 | `ITUR 14/15` | 54.00 / 54.00 | 54 +1 / 53 +1 | k = 14, 15 |
| L9 | `ITUO 00/01` | 51.00 / 51.00 | 51 +0 / 51 +0 | The same with the CPU spinning in on-chip RAM, k = 0, 1 |
| LA | `ITUO 02/03` | 52.00 / 51.00 | 52 +0 / 51 +0 | k = 2, 3 |
| LB | `ITUO 04/05` | 53.00 / 52.00 | 53 +0 / 52 +0 | k = 4, 5 |
| LC | `ITUO 06/07` | 51.00 / 51.00 | 51 +0 / 51 +1 | k = 6, 7 |
| LD | `ITU ROM/OR` | 54.00 / 52.00 | 60 +0 / 52 +0 | ITU0 compare match at TCNT 3000 to the handler's TCNT read, minus 3000: CPU spinning in ROM / on-chip RAM: the sweep above at k = 0, from the older code (it moves with where the match lands in the loop) |
| LE | `SLEEP ITU` | 54.00 / 0.00 | 54 +0 / 0 +0 | The same with the CPU in SLEEP (on-chip RAM) |
| LF | `ITUFLG R/O` | 45.00 / 13.00 | 45 +0 / 13 +0 | The compare flag (TSR IMFA) polled, no interrupt: TCNT seen minus 3000, polling from ROM / on-chip RAM |
| LG | `TSTR R/O` | 4.00 / 1.00 | 4 +0 / 1 +0 | TCNT read by the instruction right after the TSTR write that starts ITU0 from 0, from ROM / on-chip RAM |
| LH | `TCNT2 R/O` | 6.00 / 3.00 | 6 +0 / 3 +0 | Two TCNT reads back to back: their difference, from ROM / on-chip RAM |

### I: where interrupts land

Positions are the VCOUNT and HCOUNT read by the first instruction of the handler. VBR points
at a vector table in work RAM, and handlers run from ROM with the stack in work RAM unless
the line says otherwise.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| I1 | `IRQ0 -80` | 100.00 / -61.00 | 100 +0 / -62 +2 | IRQ0 compare at line 100, HCOUNT -80: VCOUNT / HCOUNT at handler entry |
| I2 | `IRQ0 200` | 100.00 / 219.00 | 100 +0 / 218 +1 | The same with HCOUNT 200 |
| I3 | `NMI V/H` | -39.00 / -65.00 | -39 +0 / -66 +1 | VCOUNT / HCOUNT at NMI handler entry |
| I4 | `IRQ1 FRAME` | -39.00 / -66.00 | -39 +0 / -66 +2 | IRQ1 from the raster signal's falling edge, frame mode: VCOUNT / HCOUNT at the handler |
| I5 | `IRQ1 LINE` | 4.00 / -67.00 | 4 +0 / -67 +0 | The same in line mode |
| I6 | `STORM A/V` | 1.00 / 348.00 | 1 +0 / 348 +0 | Level-sense IRQ1 storm in frame mode: interrupts taken during the picture / during vertical blanking (per frame) |
| I7 | `STRV N/PRG` | 303.00 / 0.00 | 303 +0 / 0 +0 | A level storm through vertical blanking: interrupts taken / loop passes the program made meanwhile (0: the storm starves it) |
| I8 | `I0 NO PA12` | 0.00 / 0.00 | 0 +0 / 0 +0 | IRQ0 with PA12 switched to a plain port pin: taken (1/0), its VCOUNT |

### R: interrupt round trips under a level-sense storm

The raster signal routed to IRQ1, in level mode, during vertical blanking. With no hold-off
after RTE, a request that stays asserted starves the program. So the program's TCNT read
after it opens the interrupt mask happens only when the storm ends. The storm's length
divided by the interrupts taken is one round trip. The six placements of program, handler
and stack add up independently on the console:

- a handler in ROM costs 4.0 cycles more than in on-chip RAM;
- a stack in work RAM costs 10.0 more;
- a program in ROM costs 4.0 more.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| R1 | `BARE ORM` | 29.28 / 1356.00 | 29.28 +0.01 / 1356 +0 | Storm round trip, handler RTE only (in ROM), stack in work RAM, program in on-chip RAM: cycles per interrupt, interrupts |
| R2 | `BARE ROM` | 33.28 / 1193.00 | 33.28 +0 / 1193 +0 | The same with the program in ROM |
| R3 | `ORM STCK` | 19.19 / 2068.00 | 19.19 +0 / 2068 +0 | Stack in on-chip RAM (program in on-chip RAM) |
| R4 | `ORM HNDL` | 25.25 / 1572.00 | 25.24 +0.01 / 1573 +0 | Handler in on-chip RAM |
| R5 | `ORM ALL` | 15.25 / 2603.00 | 15.25 +0 / 2602 +2 | Program, stack and handler all in on-chip RAM |
| R6 | `ROM OHNDL` | 29.29 / 1355.00 | 29.28 +0.01 / 1355 +1 | Program in ROM, handler in on-chip RAM |
| R7 | `PUSH3` | 61.00 / 651.00 | 61 +0.01 / 651 +0 | A handler that saves and restores three registers |
| R8 | `R3 HNDL` | 113.91 / 349.00 | 113.91 +0.01 / 349 +0 | The split-count storm handler (reads VCOUNT, counts picture and blanking interrupts in on-chip RAM, a BRA with a write in its slot), program in ROM |
| R9 | `STRV HNDL` | 131.19 / 303.00 | 131.17 +0.01 / 303 +0 | The starve handler (reads HCOUNT and VCOUNT, keeps counters in on-chip RAM) as a timed storm round trip: cycles per interrupt, interrupts |

## Part 5: DMA

### D: DMA

DMA channel 3 on auto-request.

- **Per-unit costs** are the difference between a 320-unit and a 64-unit transfer, over 256.
- **Start-up** is measured directly: a one-unit transfer copying TCNT.
- **Polls** count the CPU's poll-loop passes from on-chip RAM during the transfer. Burst
  holds the CPU off to the end. Cycle steal lets it in between units: 37 passes in 320
  units.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| D1 | `WW BST` | 6.07 | 6.07 +0.01 | DMA work RAM to work RAM, burst, cycles per unit |
| D2 | `WW CST` | 7.32 | 7.32 +0 | The same in cycle-steal mode |
| D3 | `RW BST` | 5.08 | 5.07 +0.02 | ROM to work RAM, burst |
| D4 | `WW B8` | 6.07 | 6.07 +0.01 | Work RAM to work RAM, byte units |
| D5 | `START B/C` | 9.00 / 9.00 | 9 +0 / 9 +0 | DMA start-up: a one-unit transfer copying TCNT, cycles from the TCNT read before the CHCR write to the DMAC's read of it; burst / cycle steal |
| D6 | `POLLS B/C` | 1.00 / 37.00 | 1 +0 / 37 +0 | CPU poll-loop passes (from on-chip RAM) during a 320-unit transfer, burst / cycle steal |

### X: raster DMA on DREQ0

Channel 0 on DREQ0, fed by the VDP's raster signal through PA13, as the BIOS sets it up. The
signal is switched on during the picture, where it is high, and the channel armed a line
later. So every transfer comes from a real falling edge, not from the switch-on.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| X1 | `LN N/V1` | 224.00 / 102.00 | 224 +0 / 102 +0 | Raster DMA on DREQ0, line mode, one unit per falling edge copying VCOUNT: units in one frame / first VCOUNT |
| X2 | `LN V2/VL` | 103.00 / 101.00 | 103 +0 / 101 +0 | Second and last VCOUNT copied |
| X3 | `LN H1/H2` | -83.00 / -83.00 | -84 +1 / -84 +2 | The same copying HCOUNT: first two values |
| X4 | `FR N/V` | 1.00 / -39.00 | 1 +0 / -39 +0 | Frame mode, edge: units / VCOUNT |
| X5 | `FR H` | -82.00 / 0.00 | -82 +1 / 0 +0 | Frame mode, edge: HCOUNT |
| X6 | `LV N/RATE` | 4000.00 / 8.23 | 4000 +0 / 8.23 +0 | Frame mode, level sense, 4000 units copying TCNT: units / cycles per unit |
| X7 | `LV V1/VL` | -39.00 / -3.00 | -39 +0 / -3 +0 | Level sense copying VCOUNT: first / last |

## Part 6: the picture's timing

### G: lines, frames, the raster signal and the 240-line picture

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| G1 | `LINE/FRM K` | 1019.00 / 267.92 | 1018.41 +0.68 / 267.93 +0.02 | Cycles per scanline, and per frame in thousands (224-line picture) |
| G2 | `VSTEP H/RT` | -80.00 / 293.53 | -82 +3 / 292.54 +1.41 | HCOUNT read the moment VCOUNT steps, and HCOUNT counts per 1000 CPU cycles |
| G3 | `FR FALL VH` | -39.00 / -81.00 | -39 +0 / -81 +1 | The raster signal read on PA13 as a plain input, frame mode: VCOUNT / HCOUNT where it falls |
| G4 | `FR RISE VH` | 0.00 / -80.00 | 0 +0 / -81 +1 | Frame mode: where it rises |
| G5 | `LN FALL VH` | 101.00 / -82.00 | 101 +0 / -82 +1 | Line mode: where it falls (line 101) |
| G6 | `LN RISE VH` | 101.00 / -18.00 | 101 +0 / -13 +1 | Line mode: where it rises |
| G7 | `240 NMI VH` | -23.00 / -65.00 | -23 +0 / -66 +1 | 240-line picture: NMI VCOUNT / HCOUNT |
| G8 | `240 V RNG` | -23.00 / 239.00 | -23 +0 / 239 +0 | 240-line picture: VCOUNT range seen |
| G9 | `240 FR FAL` | -23.00 / -79.00 | -23 +0 / -81 +1 | 240 lines: the frame-mode signal's fall |
| GA | `240 FR RIS` | 0.00 / -80.00 | 0 +0 / -81 +1 | 240 lines: its rise |
| GB | `240 STRV N` | 179.00 / 0.00 | 179 +0 / 0 +0 | 240 lines: the starve storm's interrupts / program passes |

## Part 7: the ADC

### A: the ADC and IRQ2

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| A1 | `ADC CH0/1` | 015A / 0234 | 0153 0155 / 0235 0237 | ADC channels 0 and 1 (hex, 10-bit; analog, this console's readings) |
| A2 | `ADC CH2/3` | 0207 / 0004 | 0206 0207 / 0014 0025 | ADC channels 2 and 3 (hex; 3 and 4 are cartridge inputs, here a flash cart's) |
| A3 | `ADC CH4/LT` | 0004 / 0148 | 0015 0022 / 0154 0154 | ADC channel 4 (hex) and cycles from the ADC trigger to IRQ2 (hex) |
| A4 | `CAPIRQ2 VH` | 101.00 / -66.00 | 101 +0 / -67 +3 | IRQ2 on scanline capture ready (line 100): VCOUNT / HCOUNT at the handler |

## Part 8: the starve handler taken apart

The starve handler (section R's `STRV HNDL`: it reads HCOUNT and VCOUNT and keeps counters in
on-chip RAM) takes about a cycle less per round trip
on the console than the sum of its pieces suggests. These sections time the pieces, then
the round trip with one thing changed at a time. See **Where the cycle goes** below.

### Z: the handler's body in pieces

Each piece of the handler's body as a loop from ROM, in cycles per sequence.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| Z1 | `HS VDP RD2` | 14.03 (spread +0.01) | 14.03 +0 | The handler's literal load of the VDP address and its two VDP reads (HCOUNT, VCOUNT) |
| Z2 | `HS ST MV ST` | 11.03 (spread +0.02) | 11.03 +0 | Its word store to on-chip RAM, a register move and a second store |
| Z3 | `HS ST ST` | 8.03 (spread +0.01) | 8.03 +0 | Two stores to on-chip RAM back to back |
| Z4 | `HS LD TST` | 7.03 (spread +0.01) | 7.03 +0 | An on-chip RAM longword load and a TST of it |
| Z5 | `HS LD T BF` | 16.03 | 16.03 +0 | The load, TST and the BF taken over the first-entry code |
| Z6 | `HS BODY` | 43.03 | 43.03 +0 | The whole body between the pushes and the pops |

### Y: its entry and exit as straight-line code

The pushes, literal loads, VDP reads and stores around the body, then the whole handler but
its RTE, as straight-line code from ROM.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| Y1 | `HE LIT LD2` | 18.03 (spread +0.01) | 18.03 +0 | Two PC-relative literal loads back to back |
| Y2 | `HE PSH3 POP` | 33.42 (spread +0.04) | 33.41 +0 | Three longword pushes to work RAM, then three pops |
| Y3 | `HE ENTRY` | 69.68 (spread +0.02) | 69.68 +0.02 | The handler's start: three pushes, a literal, two VDP reads, a literal, an on-chip RAM store (pops after) |
| Y4 | `HE ALL` | 103.96 (spread +0.05) | 103.95 +0.01 | The whole handler but RTE as straight-line code |

### Q: storm round trips with one thing changed

Timed like section R: cycles per round trip, then interrupts taken. Q1 and QA repeat R9 and
R8.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| Q1 | `STRV ORM` | 131.17 / 303.00 | 131.17 +0.01 / 303 +0 | The starve handler (ROM), program in on-chip RAM, stack in work RAM: section R's STRV HNDL again |
| Q2 | `STRV ROM` | 135.62 / 293.00 | 135.60 +0.05 / 293 +0 | The same with the program in ROM |
| Q3 | `STRV OSTK` | 113.02 / 352.00 | 113.03 +0 / 352 +0 | Program and stack in on-chip RAM, handler in ROM |
| Q4 | `STRV OHND` | 69.74 / 570.00 | 69.73 +0.02 / 570 +0 | The handler copied to on-chip RAM, program there too, stack in work RAM |
| Q5 | `NOVDP ORM` | 122.19 / 325.00 | 122.21 +0.07 / 325 +0 | The handler with its two VDP reads replaced by register moves, program in on-chip RAM |
| Q6 | `NOVDP ROM` | 127.18 / 312.00 | 127.17 +0.02 / 313 +0 | The same, program in ROM |
| Q7 | `PAD ORM` | 179.66 / 221.00 | 179.64 +0.02 / 221 +0 | The handler with 16 NOPs before its pops, program in on-chip RAM |
| Q8 | `PAD ROM` | 183.30 / 217.00 | 183.19 +0.05 / 217 +0 | The same, program in ROM |
| Q9 | `R3H ORM` | 109.83 / 362.00 | 109.82 +0.01 / 362 +0 | The split-count handler with the program in on-chip RAM |
| QA | `R3H ROM` | 113.93 / 349.00 | 113.90 +0.03 / 349 +0 | The split-count handler with the program in ROM: section R's R3 HNDL again |
| QB | `PUSH3 ROM` | 64.61 / 615.00 | 64.61 +0 / 615 +0 | Three pushes and pops, program in ROM (section R's PUSH3 has it in on-chip RAM) |
| QC | `LONG3 ORM` | 157.43 / 253.00 | 157.43 +0.01 / 253 +0 | Three pushes, 32 NOPs, three pops, from ROM; program in on-chip RAM |
| QD | `LONG3 ROM` | 161.62 / 246.00 | 161.59 +0.03 / 246 +0 | The same, program in ROM |

## Part 9: DRAM refresh

### U: what a refresh costs, by what the bus did meanwhile

A pass is one work RAM access (the same row every time), about 200 cycles of idle and a
TCNT read, so the 244-cycle refresh walks across it; two walks of 121 passes, the second a
NOP longer. The idle runs from on-chip RAM (the external bus quiet), calls 60 NOPs in
cartridge ROM, or loops on VDP reads. `MIN` is each walk's shortest pass (no refresh in
it). `H0` and `H4` count the passes 0-3 and 4-7 cycles longer than that, a hex byte each
(the last bin is 7 or more). A refresh in the idle closes the DRAM row, so the next access
pays the row again (+2 reads, +1 writes), whether the idle ran from on-chip RAM or ROM. One
that meets the access itself costs about 3 more for a read, 2 for a write.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| U1 | `RD ORM MIN` | 205.00 / 207.00 | 205 +0 / 207 +0 | A pass of one work RAM read and ~200 cycles idle in on-chip RAM: the shortest pass of each of two walks (the second a NOP longer) |
| U2 | `RD ORM H0` | 2400 / C101 | 2300 2400 / BE00 C600 | How many of its 242 passes were 0, 1 / 2, 3 cycles longer than the shortest (a hex byte each) |
| U3 | `RD ORM H4` | 020A / 0000 | 0107 0309 / 0000 0000 | The same for 4, 5 / 6, 7 or more |
| U4 | `WR ORM MIN` | 206.00 / 208.00 | 206 +0 / 208 +0 | The same with a work RAM write |
| U5 | `WR ORM H0` | 23C8 / 0106 | 23CA 24C8 / 0208 040C | Passes 0-3 cycles longer |
| U6 | `WR ORM H4` | 0000 / 0000 | 0000 0100 / 0000 0000 | Passes 4-7+ longer |
| U7 | `RD ROM MIN` | 205.00 / 207.00 | 205 +0 / 207 +0 | The idle stretch a call to 60 NOPs in ROM instead |
| U8 | `RD ROM H0` | 2300 / C401 | 2300 2400 / BC01 C501 | Passes 0-3 cycles longer |
| U9 | `RD ROM H4` | 000A / 0000 | 000E 0309 / 0000 0000 | Passes 4-7+ longer |
| UA | `WR ROM MIN` | 206.00 / 208.00 | 206 +0 / 208 +0 | A write, the idle in ROM |
| UB | `WR ROM H0` | 25C3 / 0208 | 23BB 24C9 / 0005 030C | Passes 0-3 cycles longer |
| UC | `WR ROM H4` | 0000 / 0000 | 0000 0100 / 0000 0000 | Passes 4-7+ longer |
| UD | `RD VDP MIN` | 196.00 / 196.00 | 196 +0 / 196 +0 | A read, the idle a loop of VDP reads |
| UE | `RD VDP H0` | 2E00 / C100 | 2E00 2E00 / C001 C200 | Passes 0-3 cycles longer |
| UF | `RD VDP H4` | 0201 / 0000 | 0002 0300 / 0000 0000 | Passes 4-7+ longer |
| UG | `WR VDP MIN` | 197.00 / 197.00 | 197 +0 / 197 +0 | A write, the idle VDP reads |
| UH | `WR VDP H0` | 2EC2 / 0200 | 2DBE 2EC3 / 0100 0601 | Passes 0-3 cycles longer |
| UI | `WR VDP H4` | 0000 / 0000 | 0000 0100 / 0000 0000 | Passes 4-7+ longer |

## Part 10: misaligned accesses

### E: misaligned accesses

The SH7021 takes an address error (vector 9) on a misaligned word or longword access, or a
fetch from an odd address. These run last, because a console or emulator that crashes on
them should still show everything before.

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| E1 | `AE RDB ODD` | 0.00 / 0.00 | 0 +0 / 0 +0 | Odd byte read: address errors taken / stacked PC minus the instruction's address (none expected) |
| E2 | `AE RDW ODD` | 1.00 / 4.00 | 1 +0 / 4 +0 | Odd word read: address errors / stacked PC offset |
| E3 | `AE RDL +2` | 1.00 / 4.00 | 1 +0 / 4 +0 | Longword read at 4n+2 |
| E4 | `AE WRW ODD` | 1.00 / 4.00 | 1 +0 / 4 +0 | Odd word write |
| E5 | `AE WRL +2` | 1.00 / 4.00 | 1 +0 / 4 +0 | Longword write at 4n+2 |
| E6 | `AE WR MEM` | 005A / 005A | 005A 005A / 005A 005A | The aligned words at the two misaligned write targets afterwards (hex; 1111 would be untouched: the write lands, aligned down) |
| E7 | `AE RDW ORM` | 1.00 / 4.00 | 1 +0 / 4 +0 | Odd word read of on-chip RAM |
| E8 | `AE FETCH` | 1.00 / 0.00 | 1 +0 / 0 +0 | A jump to an odd address: taken / offset |

| ID | Test | One run | 10 runs | Measures |
| --- | --- | --- | --- | --- |
| SP | `SPURIOUS` | 0.00 / 0.00 | 0 +0 / 0 +0 | Interrupts nobody asked for (0) |

## Where the cycle goes

**The starve handler's extra cycle.** Every piece of the starve handler costs on the console
what adding up the measured costs gives (sections Z and Y), yet the handler as a whole takes
about a cycle less per round trip (R9: 131.19), and so do a long plain handler and the other
variants in section Q. The cycle goes with the handler's last stack pop: RTE's pops from
work RAM normally take one state more than their reads alone, but not right after an
instruction that read work RAM itself, as a POP does. A handler that returns at once (a bare
RTE, R1-R6) pays that state; one that pops first (R7-R9, section Q) does not. Work RAM's
DRAM rows are 512 bytes.

## The end of the list

The last line on the last page is `CRC`: a CRC-32 of every result line. It shows at a glance
whether two runs agree everywhere.
