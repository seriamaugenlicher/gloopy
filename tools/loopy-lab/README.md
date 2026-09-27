# Loopy test ROMs

Two Casio Loopy test ROMs that measure a real console, for checking an emulator against the
hardware. The same ROM runs in an emulator and on a console, so the numbers compare line
for line.

- **`timing-lab.bin`**: CPU, memory, exceptions, interrupts, DMA, DRAM refresh, the
  picture's timing, the ADC and the controller port, in CPU cycles. Every test is in
  **`TIMING-LAB.md`**.
- **`video-lab.bin`**: VDP access costs and bitmap VRAM hold-ups, blending, capture,
  priority, bitmap modes, tile layouts, objects, when register and palette writes reach
  the picture, snow, and seven picture pages. Every test is in **`VIDEO-LAB.md`**.

Each comes in two builds with the same tests and IDs:

- **plain** (`timing-lab.bin`, `video-lab.bin`): every test once, named values
  (`Q1 STRV ORM 131.17 303.00`), each line marked with a green `O` if it lies inside the
  console's 10-run range or a red `X` if not. For developing an emulator and for regression
  tests.
- **stress** (`timing-lab_stress.bin`, `video-lab_stress.bin`): the whole list 10 times,
  each line shown as its ID and, per value, the lowest and how far above it the highest
  was (`Q1 131.17 +0.23 303 +1`). For baselining hardware: the console is not quite
  deterministic, and the documents give these ranges.

| ROM | On a console | In an emulator | Result pages |
| --- | --- | --- | --- |
| `timing-lab.bin` | about 15 s | 900 frames | 21 |
| `timing-lab_stress.bin` | about 100 s | 6000 frames | 21 |
| `video-lab.bin` | about 35 s | 2000 frames | 32, then 7 picture pages |
| `video-lab_stress.bin` | about 5.5 min | 20000 frames | 32, then 7 picture pages |

## Running it

- **On a console:** flash the ROM to a cartridge, plug a pad in and press nothing until
  the result pages appear. D-pad left and right turn the pages; until the first press they
  turn by themselves every 6 seconds. Each page ends with a `PAGE CRC`, a CRC-32 of its
  lines, so a photo or a transcription can be checked on its own. Nothing is written to
  cartridge SRAM.
- **In an emulator:** run it for the frames above. The results sit at work RAM
  `0x09070000`: "TLAB", the line count's low byte, the line width, the count's high byte,
  a zero, then the screen text.
