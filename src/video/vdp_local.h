#pragma once
#include <memory>
#include "video/video.h"

namespace Video
{

struct VDP
{
	//The final image handed to the frontend, in RGB565 (see display_encode in
	//render.cpp). Upstream also kept a 16bpp buffer per layer and per screen for
	//its BMP dump tooling; this fork drops them, since nothing consumes them.
	std::unique_ptr<uint16_t[]> display_output;

	//Hi-res (blend mode 3) output, assembled by compose_hires_frame. Output only:
	//never serialized; the flags are cleared by start_frame.
	std::unique_ptr<uint16_t[]> display_output_hires;
	uint8_t line_is_hires[DISPLAY_HEIGHT];
	bool frame_has_hires;

	int frame_ended;
	int visible_scanlines; //Configured by VDP_MODE

	//Screen A is 0, Screen B is 1
	uint8_t screens[2][DISPLAY_WIDTH];

	//Bitmap VRAM - 0x0C000000
	uint8_t bitmap[BITMAP_VRAM_SIZE];

	//Tile VRAM - 0x0C040000
	uint8_t tile[TILE_VRAM_SIZE];

	//OAM - 0x0C050000
	uint8_t oam[OAM_SIZE];

	//Palette - 0x0C051000
	uint8_t palette[PALETTE_SIZE];

	//Host-order decoded copy of the palette, rebuilt when dirty. The raw
	//big-endian palette above stays authoritative (MMIO reads, savestates);
	//this cache only serves the per-pixel renderer lookups.
	uint16_t palette_cache[PALETTE_SIZE / 2];
	bool palette_dirty;

	//Set on any OAM write. The renderer decodes all 128 object descriptors once
	//per change instead of re-decoding them on every scanline; see render.cpp.
	//The raw OAM above stays authoritative (MMIO reads, savestates).
	bool oam_dirty;

	//Display capture buffer - 0x0C052000
	uint8_t capture_buffer[CAPTURE_SIZE];

	//Control registers - 0x0C058xxx

	struct Mode
	{
		int use_pal;
		int extra_scanlines;
		int unk;
		int mouse_scan;
		int pad_scan;
		int unk2;
	};

	Mode mode;

	uint16_t hcount;
	uint16_t vcount;

	//Fraction of a CPU cycle left over from the lines scheduled so far, in units of
	//1/F_VDP, carried so frames keep the hardware's exact length on average
	int64_t line_cycle_remainder;

	//The current line's length in CPU cycles, and when its boundary (HCOUNT -84,
	//where VCOUNT stepped) was: HCOUNT and IRQ0 are timed from it
	int line_cycles;
	int64_t line_boundary_time;
	//Where the line boundary falls in the VDP's 8-clock bitmap VRAM slot cycle
	//(0-7): a line is 1365 VDP clocks, so it moves 5 clocks a line

	struct SyncIrqCtrl
	{
		int irq1_enable;
		int irq1_source;
	};

	SyncIrqCtrl sync_irq_ctrl;

	int capture_enable;

	//Bitmap registers - 0x0C059xxx
	struct BitmapRegs
	{
		uint16_t scrollx;
		uint16_t scrolly;
		//The scroll Y the picture uses: taken once per frame, as the frame's first
		//line starts (see start_hsync)
		uint16_t frame_scrolly;
		//The scroll X the current line uses: taken at its boundary (HCOUNT -84)
		uint16_t line_scrollx;
		uint16_t screenx;
		//The screen X the picture uses: taken once per frame, with frame_scrolly
		uint16_t frame_screenx;
		uint16_t screeny;
		uint16_t w;
		uint16_t clipx;
		uint16_t h;
		uint16_t buffer_ctrl;

		uint8_t buffered_color;
	};

	BitmapRegs bitmap_regs[4];
	uint16_t bitmap_ctrl;
	uint16_t bitmap_palsel;

	//BG/OBJ registers - 0x0C05Axxx
	struct BgCtrl
	{
		int shared_maps;
		int map_size;
		int bg0_8bit;
		int tile_size0;
		int tile_size1;
	};

	BgCtrl bg_ctrl;
	uint16_t bg_scrollx[2];
	uint16_t bg_scrolly[2];
	//What the line being drawn uses: scroll X and the enable are taken once per
	//line in its horizontal blank; scroll Y tile by tile, ahead of the beam
	uint16_t bg_scrollx_line[2];
	int bg_enable_line[2];
	static constexpr int MAX_BG_Y_SPLITS = 16;
	uint16_t bg_scrolly_start[2];
	int bg_y_split_count[2];
	int bg_y_split_x[2][MAX_BG_Y_SPLITS];
	uint16_t bg_y_split_value[2][MAX_BG_Y_SPLITS];
	uint16_t bg_palsel[2];
	uint16_t tilebase;

	struct ObjCtrl
	{
		int id_offs;
		int tile_index_offs[2];
		int is_8bit;
	};

	ObjCtrl obj_ctrl;
	uint16_t obj_palsel[2];

	//Display registers - 0x0C05Bxxx

	uint16_t dispmode;
	
	struct LayerCtrl
	{
		int bg_enable[2];
		int bitmap_enable[4];
		//The bitmap enables the picture uses: taken once per frame, with the
		//layers' frame_scrolly
		int bitmap_enable_frame[4];
		int obj_enable[2];
		int bitmap_screen_mode[2];
		int obj_screen_mode[2];
	};

	LayerCtrl layer_ctrl;

	struct ColorPrio
	{
		int prio_mode;
		int screen_b_backdrop_only;
		int output_screen_b;
		int output_screen_a;
		int blend_mode;
	};

	ColorPrio color_prio;
	uint16_t backdrops[2];

	//Palette and backdrop writes during a line: the colours before each and the
	//first pixel it reaches. The line is composited at its end, pixels before x_end
	//from the saved colours.
	struct ColorSplit
	{
		int x_end;
		uint16_t backdrops[2];
		uint16_t palette[PALETTE_SIZE / 2];
	};
	constexpr static int MAX_COLOR_SPLITS = 8;
	ColorSplit color_splits[MAX_COLOR_SPLITS];
	int color_split_count;

	struct CaptureCtrl
	{
		int scanline;
		int format;
		int raw;
	};

	CaptureCtrl capture_ctrl;

	//IRQ control registers (not to be confused with 58008) - 0x0C05Cxxx
	struct CmpIrqCtrl
	{
		int irq0_enable;
		int nmi_enable;
		int use_vcmp;
		int irq0_enable2;
		int irq2_enable;  //bits 0, 3 and 6, all three needed
		int irq2_source;  //0 = scanline capture ready, 1 = ADC ready
	};

	CmpIrqCtrl cmp_irq_ctrl;
	uint16_t irq0_hcmp;
	uint16_t irq0_vcmp;

	//DMA ctrl registers - 0x0C05Exxx
	uint16_t dma_mask;
	uint16_t dma_value;
	//BM_MEM_CTRL: bit 0 = fast bitmap VRAM access
	uint16_t bm_mem_ctrl;
	//Line-mode raster DMA signal already back high on this line
	bool raster_risen;
	//Line-mode raster DMA signal already low ahead of the next line boundary
	bool raster_fell;
};

extern VDP vdp;

}