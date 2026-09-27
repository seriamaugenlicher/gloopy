#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

#include <core/savestate.h>

namespace fs = std::filesystem;

namespace Video
{

constexpr static int DISPLAY_WIDTH = 0x100;

//A frame containing a hi-res scanline (blend mode 3) is output this wide. The
//picture keeps its physical width: each of the 256 pixels becomes two half-pixels,
//screen A then screen B, so the VDP's 256 columns carry 512.
constexpr static int HIRES_DISPLAY_WIDTH = 0x200;

//Output is always 240 lines tall, even in 224-line mode
constexpr static int DISPLAY_HEIGHT = 0xF0;

constexpr static int BITMAP_VRAM_START = 0x04000000;
constexpr static int BITMAP_VRAM_SIZE = 0x20000;
constexpr static int BITMAP_VRAM_END = BITMAP_VRAM_START + BITMAP_VRAM_SIZE;

constexpr static int TILE_VRAM_START = 0x04040000;
constexpr static int TILE_VRAM_SIZE = 0x10000;
constexpr static int TILE_VRAM_END = TILE_VRAM_START + TILE_VRAM_SIZE;

constexpr static int OAM_START = 0x04050000;
constexpr static int OAM_SIZE = 0x200;
constexpr static int OAM_END = OAM_START + OAM_SIZE;

constexpr static int PALETTE_START = 0x04051000;
constexpr static int PALETTE_SIZE = 0x200;
constexpr static int PALETTE_END = PALETTE_START + PALETTE_SIZE;

constexpr static int CAPTURE_START = 0x04052000;
constexpr static int CAPTURE_SIZE = 0x200;
constexpr static int CAPTURE_END = CAPTURE_START + CAPTURE_SIZE;

constexpr static int CTRL_REG_START = 0x04058000;
constexpr static int CTRL_REG_END = 0x04059000;

constexpr static int BITMAP_REG_START = 0x04059000;
constexpr static int BITMAP_REG_END = 0x0405A000;

constexpr static int BGOBJ_REG_START = 0x0405A000;
constexpr static int BGOBJ_REG_END = 0x0405B000;

constexpr static int DISPLAY_REG_START = 0x0405B000;
constexpr static int DISPLAY_REG_END = 0x0405C000;

constexpr static int IRQ_REG_START = 0x0405C000;
constexpr static int IRQ_REG_END = 0x0405D000;

constexpr static int DMA_CTRL_START = 0x0405E000;
constexpr static int DMA_CTRL_END = 0x0405F000;

constexpr static int DMA_START = 0x0405F000;
constexpr static int DMA_END = 0x04060000;

//SYNC_CALIBRATE, VIDEO_DEBUG and RASTER_DEBUG: write-only, and ignored. The BIOS
//writes SYNC_CALIBRATE at boot; the megadoc says emulators should ignore all three.
constexpr static int DEBUG_REG_START = 0x04060000;
constexpr static int DEBUG_REG_END = 0x04061000;

constexpr static int OBJ_COUNT = 128;

void initialize();
void shutdown();

void start_frame();
bool check_frame_end();
//BM_MEM_CTRL fast mode: bitmap VRAM accesses a cycle shorter
bool bitmap_fast_access();
//Cycles an access to bitmap VRAM starting now waits for the VDP
int bitmap_wait_cycles();
//Debug Log File: report, once per session, a tile VRAM access or palette read
//made while the picture is drawn, which snows on a console but is not drawn here
extern bool snow_reports;
void set_snow_reports(bool enabled);
void note_snow_access(uint32_t offset, bool write);

//Skip per-scanline compositing for this frame (used by frameskip)
void set_render_enabled(bool enabled);

int get_display_scanlines();
uint16_t get_background_color();
uint16_t* get_display_output();

//True when a scanline composited this frame used hi-res blending (mode 3). Such
//a frame must be delivered HIRES_DISPLAY_WIDTH wide, via compose_hires_frame.
bool frame_has_hires();

//Assembles this frame at HIRES_DISPLAY_WIDTH: hi-res scanlines as composited,
//every other composited scanline pixel-doubled, placed `top` rows down, and every
//other row up to `height` filled with `fill` (RGB565). Returns the buffer,
//HIRES_DISPLAY_WIDTH pixels per row.
uint16_t* compose_hires_frame(int drawn, int height, uint16_t fill, int top);

void save_state(SaveState::Snapshot& ss);
void load_state(SaveState::Snapshot& ss);

//TODO: should these MMIO accessors be moved to a different file?
uint8_t palette_read8(uint32_t addr);
uint16_t palette_read16(uint32_t addr);
uint32_t palette_read32(uint32_t addr);

void palette_write8(uint32_t addr, uint8_t value);
void palette_write16(uint32_t addr, uint16_t value);
void palette_write32(uint32_t addr, uint32_t value);

uint8_t oam_read8(uint32_t addr);
uint16_t oam_read16(uint32_t addr);
uint32_t oam_read32(uint32_t addr);

void oam_write8(uint32_t addr, uint8_t value);
void oam_write16(uint32_t addr, uint16_t value);
void oam_write32(uint32_t addr, uint32_t value);

uint8_t capture_read8(uint32_t addr);
uint16_t capture_read16(uint32_t addr);
uint32_t capture_read32(uint32_t addr);

void capture_write8(uint32_t addr, uint8_t value);
void capture_write16(uint32_t addr, uint16_t value);
void capture_write32(uint32_t addr, uint32_t value);

uint8_t ctrl_read8(uint32_t addr);
uint16_t ctrl_read16(uint32_t addr);
uint32_t ctrl_read32(uint32_t addr);

void ctrl_write8(uint32_t addr, uint8_t value);
void ctrl_write16(uint32_t addr, uint16_t value);
void ctrl_write32(uint32_t addr, uint32_t value);

uint8_t bitmap_reg_read8(uint32_t addr);
uint16_t bitmap_reg_read16(uint32_t addr);
uint32_t bitmap_reg_read32(uint32_t addr);

void bitmap_reg_write8(uint32_t addr, uint8_t value);
void bitmap_reg_write16(uint32_t addr, uint16_t value);
void bitmap_reg_write32(uint32_t addr, uint32_t value);

uint8_t bgobj_read8(uint32_t addr);
uint16_t bgobj_read16(uint32_t addr);
uint32_t bgobj_read32(uint32_t addr);

void bgobj_write8(uint32_t addr, uint8_t value);
void bgobj_write16(uint32_t addr, uint16_t value);
void bgobj_write32(uint32_t addr, uint32_t value);

uint8_t display_read8(uint32_t addr);
uint16_t display_read16(uint32_t addr);
uint32_t display_read32(uint32_t addr);

void display_write8(uint32_t addr, uint8_t value);
void display_write16(uint32_t addr, uint16_t value);
void display_write32(uint32_t addr, uint32_t value);

uint8_t irq_read8(uint32_t addr);
uint16_t irq_read16(uint32_t addr);
uint32_t irq_read32(uint32_t addr);

void irq_write8(uint32_t addr, uint8_t value);
void irq_write16(uint32_t addr, uint16_t value);
void irq_write32(uint32_t addr, uint32_t value);

uint8_t dma_ctrl_read8(uint32_t addr);
uint16_t dma_ctrl_read16(uint32_t addr);
uint32_t dma_ctrl_read32(uint32_t addr);

void dma_ctrl_write8(uint32_t addr, uint8_t value);
void dma_ctrl_write16(uint32_t addr, uint16_t value);
void dma_ctrl_write32(uint32_t addr, uint32_t value);

uint8_t dma_read8(uint32_t addr);
uint16_t dma_read16(uint32_t addr);
uint32_t dma_read32(uint32_t addr);

void dma_write8(uint32_t addr, uint8_t value);
void dma_write16(uint32_t addr, uint16_t value);
void dma_write32(uint32_t addr, uint32_t value);

uint8_t debug_read8(uint32_t addr);
uint16_t debug_read16(uint32_t addr);
uint32_t debug_read32(uint32_t addr);

void debug_write8(uint32_t addr, uint8_t value);
void debug_write16(uint32_t addr, uint16_t value);
void debug_write32(uint32_t addr, uint32_t value);

}  // namespace Video