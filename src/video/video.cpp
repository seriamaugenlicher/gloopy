#include <common/bswp.h>
#include <common/wordops.h>
#include <core/loopy_io.h>
#include <core/memory.h>
#include <core/sh2/peripherals/sh2_intc.h>
#include <core/timing.h>
#include <log/log.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

#include "video/render.h"
#include "video/vdp_local.h"

namespace Video
{

static Timing::FuncHandle vcount_func, hsync_func;
static Timing::EventHandle vcount_ev, hsync_ev;

//HCOUNT runs from -84 at the line boundary (the start of horizontal blanking)
//through 0 where the picture starts, to 257
constexpr static int HCOUNT_AT_BOUNDARY = -84;
constexpr static int HCOUNT_LAST = 257;

VDP vdp;

//The raster DMA signal drives the CPU's DREQ0/IRQ1/PA13 pin (active low). It is
//high over the picture: in frame mode low from VCOUNT going negative until it
//wraps to 0, in line mode low from each active line's hblank to the next picture
//and throughout VSYNC. The PFC and INTC decide whether and how the CPU sees it.
static void update_raster_dma_signal()
{
	bool low = false;
	if (vdp.sync_irq_ctrl.irq1_enable)
	{
		bool active_line = vdp.vcount < vdp.visible_scanlines;
		bool in_hblank = (vdp.hcount & 0x100) != 0;
		low = !active_line || (vdp.sync_irq_ctrl.irq1_source == 1 && ((in_hblank && !vdp.raster_risen) || vdp.raster_fell));
	}
	SH2::OCPM::INTC::set_irq1_line(low);
}

//Switching the signal on or off reaches the pin about 15 cycles after the write
constexpr int SIGNAL_SWITCH_CYCLES = 15;
static Timing::FuncHandle signal_switch_func;
static void signal_switch(uint64_t, int)
{
	update_raster_dma_signal();
}

//In line mode the signal rises before the picture, at HCOUNT -20, and falls one
//HCOUNT step (4 VDP clocks) before the next line boundary; in frame mode it rises
//and falls at the line boundary (start_hsync)
constexpr static int RASTER_LINE_RISE_HCOUNT = -20;
constexpr static int RASTER_LINE_FALL_LEAD_VDP_CLOCKS = 4;
static Timing::FuncHandle raster_rise_func;
static Timing::FuncHandle raster_fall_func;

static void raster_fall(uint64_t param, int cycles_late)
{
	vdp.raster_fell = true;
	update_raster_dma_signal();
}

static void raster_rise(uint64_t param, int cycles_late)
{
	vdp.raster_risen = true;
	update_raster_dma_signal();
	int64_t lead = ((int64_t)RASTER_LINE_FALL_LEAD_VDP_CLOCKS * vdp.line_cycles + Timing::VDP_CYCLES_PER_LINE / 2) /
		Timing::VDP_CYCLES_PER_LINE;
	int64_t due = vdp.line_boundary_time + vdp.line_cycles - lead;
	int64_t now = Timing::get_timestamp(Timing::CPU_TIMER);
	Timing::add_event(raster_fall_func, Timing::convert_cpu(std::max<int64_t>(due - now, 0)), 0, Timing::CPU_TIMER);
}

//IRQ0 fires where HCOUNT meets HCMP (and, with the vertical compare on, VCOUNT meets
//VCMP), scheduled per line from the line boundary; one HCOUNT step is 4 VDP clocks
static Timing::FuncHandle irq0_func;
static Timing::EventHandle irq0_ev;

static void fire_irq0(uint64_t param, int cycles_late)
{
	irq0_ev = Timing::EventHandle();
	if (vdp.cmp_irq_ctrl.irq0_enable && vdp.cmp_irq_ctrl.irq0_enable2 &&
		(!vdp.cmp_irq_ctrl.use_vcmp || vdp.vcount == vdp.irq0_vcmp))
	{
		SH2::OCPM::INTC::pulse_irq(SH2::OCPM::INTC::IRQ::IRQ0, 0);
	}
}

//CPU cycles from the line boundary (HCOUNT -84) to a given HCOUNT
static int64_t hcount_to_cycles(int hcount)
{
	return (int64_t)(hcount - HCOUNT_AT_BOUNDARY) * 4 * vdp.line_cycles / Timing::VDP_CYCLES_PER_LINE;
}

static void schedule_irq0()
{
	if (irq0_ev.is_valid())
	{
		Timing::cancel_event(irq0_ev);
	}
	if (!(vdp.cmp_irq_ctrl.irq0_enable && vdp.cmp_irq_ctrl.irq0_enable2))
	{
		return;
	}
	int hcmp = (vdp.irq0_hcmp & 0x100) ? (int)vdp.irq0_hcmp - 0x200 : (int)vdp.irq0_hcmp;
	if (hcmp < HCOUNT_AT_BOUNDARY || hcmp > HCOUNT_LAST)
	{
		return;
	}
	//Taken one HCOUNT step after the match (measured)
	int64_t delay = vdp.line_boundary_time + hcount_to_cycles(hcmp + 1) - Timing::get_timestamp(Timing::CPU_TIMER);
	if (delay < 0)
	{
		return;
	}
	if (delay == 0)
	{
		//A compare on the line boundary itself (HCOUNT -84): due right now
		fire_irq0(0, 0);
		return;
	}
	irq0_ev = Timing::add_event(irq0_func, Timing::convert_cpu(delay), 0, Timing::CPU_TIMER);
}

//HCOUNT now, signed (-84 at the line boundary)
static int hcount_now()
{
	int64_t elapsed = Timing::get_timestamp(Timing::CPU_TIMER) - vdp.line_boundary_time;
	return HCOUNT_AT_BOUNDARY + (int)(elapsed * Timing::VDP_CYCLES_PER_LINE / (4 * (int64_t)vdp.line_cycles));
}

//Where BG scroll X and enable are taken for the line (HCOUNT), and how far ahead of
//the beam scroll Y is fetched: from the first tile at or after HCOUNT + 11
constexpr static int BG_SCROLLX_LATCH_HCOUNT = -10;
constexpr static int BG_ENABLE_LATCH_HCOUNT = -10;
constexpr static int BG_SCROLLY_LEAD = 11;

static bool before_line_latch(int latch)
{
	return vdp.vcount < vdp.visible_scanlines && hcount_now() < latch;
}

static void write_bg_scrolly(int index, uint16_t value)
{
	if (vdp.vcount < vdp.visible_scanlines)
	{
		int x = (hcount_now() + BG_SCROLLY_LEAD + 7) & ~7;
		if (x > 0 && vdp.bg_y_split_count[index] < VDP::MAX_BG_Y_SPLITS)
		{
			int n = vdp.bg_y_split_count[index]++;
			if (n == 0)
			{
				vdp.bg_scrolly_start[index] = vdp.bg_scrolly[index];
			}
			vdp.bg_y_split_x[index][n] = x;
			vdp.bg_y_split_value[index][n] = value;
		}
	}
	vdp.bg_scrolly[index] = value;
}

//VCOUNT one line on or back (the step start_hsync and vsync_start make)
static uint16_t vcount_next(uint16_t v)
{
	int n = v + 1;
	if (n == vdp.visible_scanlines)
	{
		return (uint16_t)((n - Timing::LINES_PER_FRAME) & 0x1FF);
	}
	return (uint16_t)(n == 0x200 ? 0 : n);
}

static uint16_t vcount_prev(uint16_t v)
{
	if (v == 0)
	{
		return 0x1FF;
	}
	if (v == ((vdp.visible_scanlines - Timing::LINES_PER_FRAME) & 0x1FF))
	{
		return (uint16_t)(vdp.visible_scanlines - 1);
	}
	return (uint16_t)(v - 1);
}

//Cycles into the line at the sampling point of a register read, and the line it
//falls in (-1 the one before, +1 the next: the read samples across a boundary
//whose event has not run yet, or already has)
static int64_t read_elapsed(int *line_step)
{
	int64_t elapsed = Timing::read_time() - vdp.line_boundary_time;
	*line_step = 0;
	if (elapsed < 0)
	{
		elapsed += vdp.line_cycles;
		*line_step = -1;
	}
	else if (elapsed >= vdp.line_cycles)
	{
		elapsed -= vdp.line_cycles;
		*line_step = 1;
	}
	return elapsed;
}

static uint16_t current_vcount()
{
	int step;
	read_elapsed(&step);
	return step < 0 ? vcount_prev(vdp.vcount) : step > 0 ? vcount_next(vdp.vcount) : vdp.vcount;
}

static uint16_t current_hcount()
{
	int step;
	int64_t elapsed = read_elapsed(&step);
	int64_t steps = elapsed * Timing::VDP_CYCLES_PER_LINE / (4 * (int64_t)vdp.line_cycles);
	int hcount = HCOUNT_AT_BOUNDARY + (int)steps;
	if (hcount > HCOUNT_LAST)
	{
		hcount = HCOUNT_LAST;
	}
	return (uint16_t)(hcount & 0x1FF);
}

//Snow: on a console a CPU access to tile VRAM during the picture can blank a few
//pixels of a tile layer, and a palette read recolours the pixel being drawn. Not
//drawn; with the Debug Log File on the first such access is reported once.
bool snow_reports;
static bool snow_reported;

void set_snow_reports(bool enabled)
{
	snow_reports = enabled;
	snow_reported = false;
}

void note_snow_access(uint32_t offset, bool write)
{
	bool tile = offset >= (TILE_VRAM_START & 0xFFFFFF) && offset < (TILE_VRAM_END & 0xFFFFFF);
	bool palette_read = !write && offset >= (PALETTE_START & 0xFFFFFF) && offset < (PALETTE_END & 0xFFFFFF);
	if ((!tile && !palette_read) || snow_reported || vdp.vcount >= vdp.visible_scanlines)
	{
		return;
	}
	int64_t elapsed = Timing::get_timestamp(Timing::CPU_TIMER) - vdp.line_boundary_time;
	int hcount = HCOUNT_AT_BOUNDARY + (int)(elapsed * Timing::VDP_CYCLES_PER_LINE / (4 * (int64_t)vdp.line_cycles));
	//Tile fetches run about 8 pixels ahead of the picture
	if (hcount < (tile ? -8 : 0) || hcount >= DISPLAY_WIDTH)
	{
		return;
	}
	snow_reported = true;
	snow_reports = false;
	Log::info("[Video] %s during the picture (line %d, HCOUNT %d): snows on a real Loopy, not drawn here (reported once)",
		tile ? (write ? "tile VRAM written" : "tile VRAM read") : "palette read", (int)vdp.vcount, hcount);
}

//Bitmap VRAM hold-ups, measured by beam position on a console.
//The VDP fetches bitmap data in bursts locked to the line: each 20 VDP clocks it
//holds the bus for 7, frees it for 2, holds it for 7 and frees it for 4, and an
//access that meets a hold waits for its end (0-5 cycles). With no bitmap layer
//shown four bursts run across the line boundary; with one shown two run there and
//two more late in horizontal blanking. Positions are VDP clocks from the line
//boundary, as timed here at the start of the accessing instruction.
constexpr static int BURST_HOLD_A = 7;
constexpr static int BURST_FREE_A = 2;
constexpr static int BURST_HOLD_B = 7;
constexpr static int BURST_LEN = 20;
constexpr static int BURSTS_START = -2;
constexpr static int BURSTS_LATE_START = 306;

//Wait for an access `pos` (VDP clocks x SUB from the boundary) in bursts from `start`
static int burst_wait(int64_t pos, int start, int bursts)
{
	constexpr int64_t SUB = 64;
	int64_t p = pos - start * SUB;
	if (p < 0 || p >= bursts * BURST_LEN * SUB)
	{
		return -1;
	}
	int64_t q = p % (BURST_LEN * SUB);
	int64_t end;
	if (q < BURST_HOLD_A * SUB)
	{
		end = BURST_HOLD_A * SUB;
	}
	else if (q >= (BURST_HOLD_A + BURST_FREE_A) * SUB && q < (BURST_HOLD_A + BURST_FREE_A + BURST_HOLD_B) * SUB)
	{
		end = (BURST_HOLD_A + BURST_FREE_A + BURST_HOLD_B) * SUB;
	}
	else
	{
		return 0;
	}
	return (int)((end - q) * vdp.line_cycles / (Timing::VDP_CYCLES_PER_LINE * SUB)) + 1;
}

int bitmap_wait_cycles()
{
	constexpr int64_t SUB = 64;
	int line = (vdp.vcount & 0x100) ? (int)vdp.vcount - 0x200 : (int)vdp.vcount;
	int64_t elapsed = Timing::get_timestamp(Timing::CPU_TIMER) - vdp.line_boundary_time;
	int64_t pos = elapsed * Timing::VDP_CYCLES_PER_LINE * SUB / vdp.line_cycles;
	bool bitmap_shown = false;
	for (int i = 0; i < 4; i++)
	{
		bitmap_shown |= vdp.layer_ctrl.bitmap_enable_frame[i] != 0;
	}
	//End of a line, fetching for the next one (lines -1 to the next-to-last)
	bool pre_line = line >= -1 && line <= vdp.visible_scanlines - 2;
	//Start of a picture line
	bool this_line = line >= 0 && line <= vdp.visible_scanlines - 1;
	int bursts = bitmap_shown ? 2 : 4;
	int wait = -1;
	if (pre_line)
	{
		wait = burst_wait(pos - Timing::VDP_CYCLES_PER_LINE * SUB, BURSTS_START, bursts);
	}
	if (wait < 0 && this_line)
	{
		wait = burst_wait(pos, BURSTS_START, bursts);
	}
	if (wait < 0 && this_line && bitmap_shown)
	{
		wait = burst_wait(pos, BURSTS_LATE_START, 2);
	}
	return wait < 0 ? 0 : std::min(wait, 5);
}


static bool render_enabled = true;

void set_render_enabled(bool enabled)
{
	render_enabled = enabled;
}

static void vsync_start();

//From the trigger to the result and IRQ2 (measured)
constexpr static int ADC_CONVERSION_CYCLES = 254;
static Timing::FuncHandle adc_func;
static Timing::EventHandle adc_ev;

static void adc_ready(uint64_t param, int cycles_late)
{
	adc_ev = Timing::EventHandle();
	LoopyIO::update_print_temp();
	if (vdp.cmp_irq_ctrl.irq2_enable && vdp.cmp_irq_ctrl.irq2_source == 1)
	{
		SH2::OCPM::INTC::pulse_irq(SH2::OCPM::INTC::IRQ::IRQ2, 0);
	}
}

//The line boundary, at the start of horizontal blanking (HCOUNT -84): VCOUNT steps,
//NMI and the raster DMA frame edge come from here, and the line just drawn is
//composited
static void start_hsync(uint64_t param, int cycles_late)
{
	vdp.hcount |= 0x100;
	vdp.line_boundary_time = Timing::get_timestamp(Timing::CPU_TIMER) - cycles_late;

	//With rendering off (frameskip) a scanline produces nothing the machine can
	//observe - with one exception. Display
	//capture copies the composited screen into a buffer the game reads back
	//through MMIO, so it is emulation state, not just output. Compositing an
	//armed capture scanline keeps that path exact even on a skipped frame; the
	//capture only ever reads the screens for its own scanline.
	bool capture_scanline = vdp.capture_enable && vdp.vcount == vdp.capture_ctrl.scanline;
	if (vdp.vcount < vdp.visible_scanlines && (render_enabled || capture_scanline))
	{
		Renderer::draw_scanline(vdp.vcount);
	}
	vdp.color_split_count = 0;
	for (int i = 0; i < 2; i++)
	{
		vdp.bg_y_split_count[i] = 0;
		vdp.bg_scrollx_line[i] = vdp.bg_scrollx[i];
		vdp.bg_enable_line[i] = vdp.layer_ctrl.bg_enable[i];
	}
	//Capture ready raises IRQ2 as the captured line ends
	if (capture_scanline && vdp.cmp_irq_ctrl.irq2_enable && vdp.cmp_irq_ctrl.irq2_source == 0)
	{
		SH2::OCPM::INTC::pulse_irq(SH2::OCPM::INTC::IRQ::IRQ2, 0);
	}

	vdp.vcount++;

	//A bitmap layer's horizontal scroll is taken per line, here
	for (auto& layer : vdp.bitmap_regs)
	{
		layer.line_scrollx = layer.scrollx;
	}

	//Once we go past the visible region, enter VSYNC
	if (vdp.vcount == vdp.visible_scanlines)
	{
		vsync_start();
	}

	//At the end of VSYNC, wrap around to the start of the visible region
	constexpr static int VSYNC_END = 0x200;

	if (vdp.vcount == VSYNC_END)
	{
		Log::debug("[Video] VSYNC end");
		vdp.vcount = 0;

		//A bitmap layer's vertical scroll, screen X and enable are taken once per
		//frame, here
		for (int i = 0; i < 4; i++)
		{
			auto& layer = vdp.bitmap_regs[i];
			layer.frame_scrolly = layer.scrolly;
			layer.frame_screenx = layer.screenx;
			vdp.layer_ctrl.bitmap_enable_frame[i] = vdp.layer_ctrl.bitmap_enable[i];
		}
	}

	//Frame mode raster DMA falls with VCOUNT going negative; line mode falls at
	//every line boundary and rises again before the picture
	vdp.raster_risen = false;
	vdp.raster_fell = false;
	update_raster_dma_signal();
	if (vdp.sync_irq_ctrl.irq1_enable && vdp.sync_irq_ctrl.irq1_source == 1 && vdp.vcount < vdp.visible_scanlines)
	{
		Timing::add_event(raster_rise_func, Timing::convert_cpu(hcount_to_cycles(RASTER_LINE_RISE_HCOUNT) - cycles_late), 0,
						  Timing::CPU_TIMER);
	}
	schedule_irq0();
}

static void vsync_start()
{
	Log::debug("[Video] VSYNC start");

	//This is kinda weird, but when the VDP enters VSYNC, the total number of scanlines is subtracted from VCOUNT
	//Think of the VSYNC lines as being negative
	vdp.vcount = (vdp.vcount - Timing::LINES_PER_FRAME) & 0x1FF;
	vdp.frame_ended = true;

	//NMI is a pulse on VSYNC. The CPU latches the edge, so it is taken even if
	//the game is busy with SR raised when it arrives.
	if (vdp.cmp_irq_ctrl.nmi_enable)
	{
		SH2::OCPM::INTC::pulse_irq(SH2::OCPM::INTC::IRQ::NMI, 0);
	}
}

//The picture starts (HCOUNT 0): the raster DMA signal rises on active lines, and
//the next line's events are scheduled
static void inc_vcount(uint64_t param, int cycles_late)
{
	//Leave HSYNC
	vdp.hcount &= ~0x100;

	update_raster_dma_signal();

	//A line is not a whole number of CPU cycles (about 1018.9 at the hardware rate,
	//1015.9 at 60 Hz); the fraction is carried so a frame adds up exactly
	int64_t line_num, line_den;
	if (Timing::hardware_refresh())
	{
		line_num = Timing::CPU_CYCLES_PER_FRAME;
		line_den = Timing::LINES_PER_FRAME;
	}
	else
	{
		line_num = Timing::CPU_CYCLES_PER_FRAME * Timing::F_VDP;
		line_den = (int64_t)Timing::VDP_CYCLES_PER_LINE * Timing::LINES_PER_FRAME * 60 * Timing::LINES_PER_FRAME;
	}
	line_num += vdp.line_cycle_remainder;
	int line_cycles = (int)(line_num / line_den);
	vdp.line_cycle_remainder = line_num % line_den;
	vdp.line_cycles = line_cycles;

	//HSYNC begins 256 of the line's 341.25 pixels in
	int hsync_cycles_in = line_cycles * 1024 / Timing::VDP_CYCLES_PER_LINE;

	Timing::UnitCycle scanline_cycles = Timing::convert_cpu(line_cycles - cycles_late);
	vcount_ev = Timing::add_event(vcount_func, scanline_cycles, 0, Timing::CPU_TIMER);

	Timing::UnitCycle hsync_cycles = Timing::convert_cpu(hsync_cycles_in - cycles_late);
	hsync_ev = Timing::add_event(hsync_func, hsync_cycles, 0, Timing::CPU_TIMER);
}

void initialize()
{
	vdp = {};

	vdp.visible_scanlines = 0xE0;
	vdp.palette_dirty = true;
	vdp.oam_dirty = true;

	//Set all OBJs to invisible
	for (int i = 0; i < OAM_SIZE; i += 4)
	{
		oam_write32(i, 0x200);
	}

	vdp.display_output = std::make_unique<uint16_t[]>(DISPLAY_WIDTH * DISPLAY_HEIGHT);
	vdp.display_output_hires = std::make_unique<uint16_t[]>(HIRES_DISPLAY_WIDTH * DISPLAY_HEIGHT);
	Renderer::reset();

	//Map VRAM to the CPU
	//Bitmap VRAM is mirrored
	Memory::map_sh2_pagetable(vdp.bitmap, BITMAP_VRAM_START, BITMAP_VRAM_SIZE);
	Memory::map_sh2_pagetable(vdp.bitmap, BITMAP_VRAM_START + BITMAP_VRAM_SIZE, BITMAP_VRAM_SIZE);
	Memory::map_sh2_pagetable(vdp.tile, TILE_VRAM_START, TILE_VRAM_SIZE);

	vcount_func = Timing::register_func("Video::inc_vcount", inc_vcount);
	hsync_func = Timing::register_func("Video::start_hsync", start_hsync);
	irq0_func = Timing::register_func("Video::fire_irq0", fire_irq0);
	irq0_ev = Timing::EventHandle();
	adc_func = Timing::register_func("Video::adc_ready", adc_ready);
	raster_rise_func = Timing::register_func("Video::raster_rise", raster_rise);
	raster_fall_func = Timing::register_func("Video::raster_fall", raster_fall);
	signal_switch_func = Timing::register_func("Video::signal_switch", signal_switch);
	adc_ev = Timing::EventHandle();

	//Kickstart the VCOUNT event
	inc_vcount(0, 0);
}

void shutdown()
{
	// nop
}

void start_frame()
{
	vdp.frame_ended = false;

	//Hi-res tracking is per delivered frame. Every scanline composited between here
	//and VSYNC belongs to this frame, and flags itself if it uses blend mode 3.
	vdp.frame_has_hires = false;
	memset(vdp.line_is_hires, 0, sizeof(vdp.line_is_hires));

	//The display buffer is deliberately not cleared: every scanline in the
	//visible area is fully rewritten as it is composited, and the rows beyond it
	//(only shown when overscan cropping is off) are filled with the backdrop when
	//the frame is handed to the frontend. Upstream cleared it here, but passed an
	//element count where a byte count was wanted, so it only ever cleared half the
	//buffer - which is how it went unnoticed that the clear was doing nothing.
}

bool check_frame_end()
{
	return vdp.frame_ended;
}

uint16_t get_background_color()
{
	return vdp.backdrops[0];
}

int get_display_scanlines()
{
	return vdp.visible_scanlines;
}

uint16_t* get_display_output()
{
	return vdp.display_output.get();
}

bool frame_has_hires()
{
	return vdp.frame_has_hires;
}

uint16_t* compose_hires_frame(int drawn, int height, uint16_t fill, int top)
{
	//Hi-res rows are already in this buffer; every other row is pixel-doubled into
	//it. Bottom-up, so moving a row down by `top` never overwrites one not yet moved.
	uint16_t* frame = vdp.display_output_hires.get();
	const uint16_t* normal = vdp.display_output.get();
	for (int y = height - 1; y >= 0; y--)
	{
		uint16_t* dst = frame + (size_t)y * HIRES_DISPLAY_WIDTH;
		int src_y = y - top;
		if (src_y < 0 || src_y >= drawn)
		{
			std::fill_n(dst, HIRES_DISPLAY_WIDTH, fill);
		}
		else if (vdp.line_is_hires[src_y])
		{
			if (top)
			{
				memcpy(dst, frame + (size_t)src_y * HIRES_DISPLAY_WIDTH, HIRES_DISPLAY_WIDTH * sizeof(uint16_t));
			}
		}
		else
		{
			const uint16_t* src = normal + (size_t)src_y * DISPLAY_WIDTH;
			for (int x = 0; x < DISPLAY_WIDTH; x++)
			{
				dst[x * 2] = src[x];
				dst[x * 2 + 1] = src[x];
			}
		}
	}
	return frame;
}

void save_state(SaveState::Snapshot& ss)
{
	ss.begin_section(SaveState::fourcc("VDP "));

	//VRAM and register state; the layer/screen output buffers are transient
	//(regenerated every frame) and are not serialized
	ss.write_blob(vdp.bitmap, BITMAP_VRAM_SIZE);
	ss.write_blob(vdp.tile, TILE_VRAM_SIZE);
	ss.write_blob(vdp.oam, OAM_SIZE);
	ss.write_blob(vdp.palette, PALETTE_SIZE);
	ss.write_blob(vdp.capture_buffer, CAPTURE_SIZE);
	ss.write(vdp.screens);

	ss.write(vdp.frame_ended);
	ss.write(vdp.visible_scanlines);
	ss.write(vdp.mode);
	ss.write(vdp.hcount);
	ss.write(vdp.vcount);
	ss.write(vdp.line_cycle_remainder);
	ss.write(vdp.line_cycles);
	ss.write(vdp.line_boundary_time);
	ss.write(vdp.sync_irq_ctrl);
	ss.write(vdp.capture_enable);
	ss.write(vdp.bitmap_regs);
	ss.write(vdp.bitmap_ctrl);
	ss.write(vdp.bitmap_palsel);
	ss.write(vdp.bg_ctrl);
	ss.write(vdp.bg_scrollx);
	ss.write(vdp.bg_scrolly);
	ss.write(vdp.bg_scrollx_line);
	ss.write(vdp.bg_enable_line);
	ss.write(vdp.bg_scrolly_start);
	ss.write(vdp.bg_y_split_count);
	ss.write(vdp.bg_y_split_x);
	ss.write(vdp.bg_y_split_value);
	ss.write(vdp.bg_palsel);
	ss.write(vdp.tilebase);
	ss.write(vdp.obj_ctrl);
	ss.write(vdp.obj_palsel);
	ss.write(vdp.dispmode);
	ss.write(vdp.layer_ctrl);
	ss.write(vdp.color_prio);
	ss.write(vdp.backdrops);
	ss.write(vdp.capture_ctrl);
	ss.write(vdp.cmp_irq_ctrl);
	ss.write(vdp.irq0_hcmp);
	ss.write(vdp.irq0_vcmp);
	ss.write(vdp.dma_mask);
	ss.write(vdp.dma_value);
	ss.write(vdp.bm_mem_ctrl);
	ss.write(vdp.raster_risen);
	ss.write(vdp.raster_fell);
}

void load_state(SaveState::Snapshot& ss)
{
	ss.expect_section(SaveState::fourcc("VDP "));

	ss.read_blob(vdp.bitmap, BITMAP_VRAM_SIZE);
	ss.read_blob(vdp.tile, TILE_VRAM_SIZE);
	ss.read_blob(vdp.oam, OAM_SIZE);
	ss.read_blob(vdp.palette, PALETTE_SIZE);
	vdp.palette_dirty = true;
	vdp.oam_dirty = true;
	ss.read_blob(vdp.capture_buffer, CAPTURE_SIZE);
	ss.read(vdp.screens);

	ss.read(vdp.frame_ended);
	ss.read(vdp.visible_scanlines);
	ss.read(vdp.mode);
	ss.read(vdp.hcount);
	ss.read(vdp.vcount);
	ss.read(vdp.line_cycle_remainder);
	ss.read(vdp.line_cycles);
	ss.read(vdp.line_boundary_time);
	vdp.color_split_count = 0;
	ss.read(vdp.sync_irq_ctrl);
	ss.read(vdp.capture_enable);
	ss.read(vdp.bitmap_regs);
	ss.read(vdp.bitmap_ctrl);
	ss.read(vdp.bitmap_palsel);
	ss.read(vdp.bg_ctrl);
	ss.read(vdp.bg_scrollx);
	ss.read(vdp.bg_scrolly);
	ss.read(vdp.bg_scrollx_line);
	ss.read(vdp.bg_enable_line);
	ss.read(vdp.bg_scrolly_start);
	ss.read(vdp.bg_y_split_count);
	ss.read(vdp.bg_y_split_x);
	ss.read(vdp.bg_y_split_value);
	ss.read(vdp.bg_palsel);
	ss.read(vdp.tilebase);
	ss.read(vdp.obj_ctrl);
	ss.read(vdp.obj_palsel);
	ss.read(vdp.dispmode);
	ss.read(vdp.layer_ctrl);
	ss.read(vdp.color_prio);
	ss.read(vdp.backdrops);
	ss.read(vdp.capture_ctrl);
	ss.read(vdp.cmp_irq_ctrl);
	ss.read(vdp.irq0_hcmp);
	ss.read(vdp.irq0_vcmp);
	ss.read(vdp.dma_mask);
	ss.read(vdp.dma_value);
	ss.read(vdp.bm_mem_ctrl);
	ss.read(vdp.raster_risen);
	ss.read(vdp.raster_fell);

	LoopyIO::set_controller_scan_mode(vdp.mode.pad_scan, vdp.mode.mouse_scan);
}

uint8_t palette_read8(uint32_t addr)
{
	return vdp.palette[addr & 0x1FF];
}

uint16_t palette_read16(uint32_t addr)
{
	uint16_t value;
	memcpy(&value, &vdp.palette[addr & 0x1FE], 2);
	return Common::bswp16(value);
}

uint32_t palette_read32(uint32_t addr)
{
	uint32_t value;
	memcpy(&value, &vdp.palette[addr & 0x1FE], 4);
	return Common::bswp32(value);
}

//A palette or backdrop write during the picture reaches the line this far after
//the write's HCOUNT, in quarter counts (measured)
constexpr static int SPLIT_Q = 4;
constexpr static int PALETTE_SPLIT_OFFSET_Q = 6;
constexpr static int BACKDROP_SPLIT_OFFSET_Q = 2;

static void split_line_colors(int offset_q)
{
	if (vdp.vcount >= vdp.visible_scanlines || vdp.color_split_count >= VDP::MAX_COLOR_SPLITS)
	{
		return;
	}
	int64_t elapsed = Timing::get_timestamp(Timing::CPU_TIMER) - vdp.line_boundary_time;
	int64_t pos_q = HCOUNT_AT_BOUNDARY * SPLIT_Q +
		elapsed * Timing::VDP_CYCLES_PER_LINE * SPLIT_Q / (4 * (int64_t)vdp.line_cycles) + offset_q;
	if (pos_q <= 0)
	{
		//Before the first pixel: the whole line has the new colour
		return;
	}
	int x = (int)std::min<int64_t>(pos_q / SPLIT_Q, DISPLAY_WIDTH);
	if (vdp.color_split_count && vdp.color_splits[vdp.color_split_count - 1].x_end >= x)
	{
		return;
	}
	auto& split = vdp.color_splits[vdp.color_split_count++];
	split.x_end = x;
	split.backdrops[0] = vdp.backdrops[0];
	split.backdrops[1] = vdp.backdrops[1];
	for (int i = 0; i < PALETTE_SIZE / 2; i++)
	{
		uint16_t color;
		memcpy(&color, vdp.palette + i * 2, 2);
		split.palette[i] = Common::bswp16(color);
	}
}

void palette_write8(uint32_t addr, uint8_t value)
{
	split_line_colors(PALETTE_SPLIT_OFFSET_Q);
	vdp.palette[addr & 0x1FF] = value;
	vdp.palette_dirty = true;
}

void palette_write16(uint32_t addr, uint16_t value)
{
	split_line_colors(PALETTE_SPLIT_OFFSET_Q);
	//15-bit colours: bit 15 is not stored
	value = Common::bswp16((uint16_t)(value & 0x7FFF));
	memcpy(&vdp.palette[addr & 0x1FE], &value, 2);
	vdp.palette_dirty = true;
}

void palette_write32(uint32_t addr, uint32_t value)
{
	split_line_colors(PALETTE_SPLIT_OFFSET_Q);
	value = Common::bswp32(value);
	memcpy(&vdp.palette[addr & 0x1FE], &value, 4);
	vdp.palette_dirty = true;
}

uint8_t oam_read8(uint32_t addr)
{
	return vdp.oam[addr & 0x1FF];
}

uint16_t oam_read16(uint32_t addr)
{
	uint16_t value;
	memcpy(&value, &vdp.oam[addr & 0x1FE], 2);
	return Common::bswp16(value);
}

uint32_t oam_read32(uint32_t addr)
{
	uint32_t value;
	memcpy(&value, &vdp.oam[addr & 0x1FE], 4);
	return Common::bswp32(value);
}

void oam_write8(uint32_t addr, uint8_t value)
{
	vdp.oam[addr & 0x1FF] = value;
	vdp.oam_dirty = true;
}

void oam_write16(uint32_t addr, uint16_t value)
{
	value = Common::bswp16(value);
	memcpy(&vdp.oam[addr & 0x1FE], &value, 2);
	vdp.oam_dirty = true;
}

void oam_write32(uint32_t addr, uint32_t value)
{
	value = Common::bswp32(value);
	memcpy(&vdp.oam[addr & 0x1FE], &value, 4);
	vdp.oam_dirty = true;
}

uint8_t capture_read8(uint32_t addr)
{
	return vdp.capture_buffer[addr & 0x1FF];
}

uint16_t capture_read16(uint32_t addr)
{
	uint16_t value;
	memcpy(&value, &vdp.capture_buffer[addr & 0x1FE], 2);
	return Common::bswp16(value);
}

uint32_t capture_read32(uint32_t addr)
{
	uint32_t value;
	memcpy(&value, &vdp.capture_buffer[addr & 0x1FE], 4);
	return Common::bswp32(value);
}

void capture_write8(uint32_t addr, uint8_t value)
{
	LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
}

void capture_write16(uint32_t addr, uint16_t value)
{
	LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
}

void capture_write32(uint32_t addr, uint32_t value)
{
	LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
}

uint8_t bitmap_reg_read8(uint32_t addr)
{
	READ_HALFWORD(bitmap_reg, addr);
}

uint16_t bitmap_reg_read16(uint32_t addr)
{
	addr &= 0xFFE;

	int index = (addr >> 1) & 0x3;
	auto layer = &vdp.bitmap_regs[index];
	int reg = addr & ~0x7;

	switch (reg)
	{
	case 0x000:
		return layer->scrollx;
	case 0x008:
		return layer->scrolly;
	case 0x010:
		return layer->screenx;
	case 0x018:
		return layer->screeny;
	case 0x020:
		return layer->w | (layer->clipx << 8);
	case 0x028:
		return layer->h;
	case 0x030:
		return vdp.bitmap_ctrl;
	case 0x040:
		return vdp.bitmap_palsel;
	case 0x050:
		return layer->buffer_ctrl;
	default:
		LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
		return 0;
	}
}

uint32_t bitmap_reg_read32(uint32_t addr)
{
	READ_DOUBLEWORD(bitmap_reg, addr);
}

void bitmap_reg_write8(uint32_t addr, uint8_t value)
{
	WRITE_HALFWORD(bitmap_reg, addr, value);
}

void bitmap_reg_write16(uint32_t addr, uint16_t value)
{
	addr &= 0xFFE;

	int index = (addr >> 1) & 0x3;
	auto layer = &vdp.bitmap_regs[index];
	int reg = addr & ~0x7;

	switch (reg)
	{
	case 0x000:
		Log::debug("[Video] write BM%d_SCROLLX: %04X", index, value);
		layer->scrollx = value & 0x1FF;
		break;
	case 0x008:
		Log::debug("[Video] write BM%d_SCROLLY: %04X", index, value);
		layer->scrolly = value & 0x1FF;
		break;
	case 0x010:
		Log::debug("[Video] write BM%d_SCREENX: %04X", index, value);
		layer->screenx = value & 0x1FF;
		break;
	case 0x018:
		Log::debug("[Video] write BM%d_SCREENY: %04X", index, value);
		layer->screeny = value & 0x1FF;
		break;
	case 0x020:
		Log::debug("[Video] write BM%d_CLIPWIDTH: %04X", index, value);
		layer->w = value & 0xFF;
		layer->clipx = value >> 8;
		break;
	case 0x028:
		Log::debug("[Video] write BM%d_HEIGHT: %04X", index, value);
		layer->h = value & 0xFF;
		break;
	case 0x030:
		Log::debug("[Video] write BM_CTRL: %04X", value);
		//Three bits, the bitmap mode
		vdp.bitmap_ctrl = value & 0x7;
		break;
	case 0x040:
		Log::debug("[Video] write BM_PALSEL: %04X", value);
		vdp.bitmap_palsel = value;
		break;
	case 0x050:
		Log::debug("[Video] write BM%d_BUFFER_CTRL: %04X", index, value);
		layer->buffer_ctrl = value;
		break;
	default:
		LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
	}
}

void bitmap_reg_write32(uint32_t addr, uint32_t value)
{
	WRITE_DOUBLEWORD(bitmap_reg, addr, value);
}

uint8_t ctrl_read8(uint32_t addr)
{
	READ_HALFWORD(ctrl, addr);
}

uint16_t ctrl_read16(uint32_t addr)
{
	addr &= 0xFFE;
	switch (addr)
	{
	case 0x000:
	{
		uint16_t result = vdp.mode.use_pal;
		result |= vdp.mode.extra_scanlines << 1;
		result |= vdp.mode.unk << 2;
		result |= vdp.mode.mouse_scan << 3;
		result |= vdp.mode.pad_scan << 4;
		result |= vdp.mode.unk2 << 5;
		return result;
	}
	case 0x002:
		return current_hcount();
	case 0x004:
		return current_vcount();
	default:
		LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
		return 0;
	}
}

uint32_t ctrl_read32(uint32_t addr)
{
	READ_DOUBLEWORD(ctrl, addr);
}

void ctrl_write8(uint32_t addr, uint8_t value)
{
	WRITE_HALFWORD(ctrl, addr, value);
}

void ctrl_write16(uint32_t addr, uint16_t value)
{
	addr &= 0xFFE;
	switch (addr)
	{
	case 0x000:
		Log::debug("[Video] write MODE: %04X", value);
		vdp.mode.use_pal = value & 0x1;
		vdp.mode.extra_scanlines = (value >> 1) & 0x1;
		vdp.mode.unk = (value >> 2) & 0x1;
		vdp.mode.mouse_scan = (value >> 3) & 0x1;
		vdp.mode.pad_scan = (value >> 4) & 0x1;
		vdp.mode.unk2 = (value >> 5) & 0x1;
		if (vdp.mode.use_pal)
		{
			LOG_UNEMULATED("[Video] VDP MODE bit 0 (PAL) is not emulated: the Loopy was only sold for NTSC");
		}

		vdp.visible_scanlines = (vdp.mode.extra_scanlines) ? 0xF0 : 0xE0;
		LoopyIO::set_controller_scan_mode(vdp.mode.pad_scan, vdp.mode.mouse_scan);
		break;
	case 0x006:
		if (value & 0x01)
		{
			vdp.capture_enable = true;
		}

		if (value & 0x02)
		{
			//A conversion completes 250 CPU cycles after the trigger (measured)
			if (adc_ev.is_valid())
			{
				Timing::cancel_event(adc_ev);
			}
			adc_ev = Timing::add_event(adc_func, Timing::convert_cpu(ADC_CONVERSION_CYCLES), 0, Timing::CPU_TIMER);
		}

		if (value & 0x04)
		{
			LoopyIO::update_sensors();
		}

		//Only log writes to unimplemented bits
		if (value & ~0x0007)
		{
			Log::debug("[Video] write ctrl 006: %04X", value);
		}
		break;
	case 0x008:
		Log::debug("[Video] write SYNC_IRQ_CTRL: %04X", value);
		vdp.sync_irq_ctrl.irq1_enable = value & 0x1;
		vdp.sync_irq_ctrl.irq1_source = (value >> 1) & 0x1;
		Timing::add_event(signal_switch_func, Timing::convert_cpu(SIGNAL_SWITCH_CYCLES), 0, Timing::CPU_TIMER);
		break;
	default:
		LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
	}
}

void ctrl_write32(uint32_t addr, uint32_t value)
{
	WRITE_DOUBLEWORD(ctrl, addr, value);
}

uint8_t bgobj_read8(uint32_t addr)
{
	READ_HALFWORD(bgobj, addr);
}

uint16_t bgobj_read16(uint32_t addr)
{
	addr &= 0xFFE;
	switch (addr)
	{
	case 0x000:
	{
		uint16_t result = vdp.bg_ctrl.shared_maps;
		result |= vdp.bg_ctrl.map_size << 1;
		result |= vdp.bg_ctrl.bg0_8bit << 3;
		result |= vdp.bg_ctrl.tile_size1 << 4;
		result |= vdp.bg_ctrl.tile_size0 << 6;
		return result;
	}
	case 0x002:
		return vdp.bg_scrollx[0];
	case 0x004:
		return vdp.bg_scrolly[0];
	case 0x006:
		return vdp.bg_scrollx[1];
	case 0x008:
		return vdp.bg_scrolly[1];
	case 0x00A:
		return vdp.bg_palsel[0];
	case 0x00C:
		return vdp.bg_palsel[1];
	case 0x010:
	{
		uint16_t result = vdp.obj_ctrl.id_offs;
		result |= vdp.obj_ctrl.tile_index_offs[1] << 8;
		result |= vdp.obj_ctrl.tile_index_offs[0] << 11;
		result |= vdp.obj_ctrl.is_8bit << 14;
		return result;
	}
	case 0x012:
		return vdp.obj_palsel[0];
	case 0x014:
		return vdp.obj_palsel[1];
	case 0x020:
		return vdp.tilebase;
	default:
		LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
		return 0;
	}
}

uint32_t bgobj_read32(uint32_t addr)
{
	READ_DOUBLEWORD(bgobj, addr);
}

void bgobj_write8(uint32_t addr, uint8_t value)
{
	WRITE_HALFWORD(bgobj, addr, value);
}

void bgobj_write16(uint32_t addr, uint16_t value)
{
	addr &= 0xFFE;
	switch (addr)
	{
	case 0x000:
		Log::debug("[Video] write BG_CTRL: %04X", value);
		vdp.bg_ctrl.shared_maps = value & 0x1;
		vdp.bg_ctrl.map_size = (value >> 1) & 0x3;
		vdp.bg_ctrl.bg0_8bit = (value >> 3) & 0x1;

		//Note the reversed order!
		vdp.bg_ctrl.tile_size1 = (value >> 4) & 0x3;
		vdp.bg_ctrl.tile_size0 = (value >> 6) & 0x3;
		break;
	case 0x002:
	case 0x006:
	{
		int index = (addr - 0x002) >> 2;
		Log::debug("[Video] write BG%d_SCROLLX: %04X", index, value);
		vdp.bg_scrollx[index] = value & 0xFFF;
		if (before_line_latch(BG_SCROLLX_LATCH_HCOUNT) || vdp.vcount >= vdp.visible_scanlines)
		{
			vdp.bg_scrollx_line[index] = vdp.bg_scrollx[index];
		}
		break;
	}
	case 0x004:
	case 0x008:
	{
		int index = (addr - 0x004) >> 2;
		Log::debug("[Video] write BG%d_SCROLLY: %04X", index, value);
		write_bg_scrolly(index, value & 0xFFF);
		break;
	}
	case 0x00A:
	case 0x00C:
	{
		int index = (addr - 0x00A) >> 1;
		Log::debug("[Video] write BG%d_PALSEL: %04X", index, value);
		vdp.bg_palsel[index] = value;
		break;
	}
	case 0x010:
		Log::debug("[Video] write OBJ_CTRL: %04X", value);
		vdp.obj_ctrl.id_offs = value & 0xFF;

		//Note the reversed order!
		vdp.obj_ctrl.tile_index_offs[1] = (value >> 8) & 0x7;
		vdp.obj_ctrl.tile_index_offs[0] = (value >> 11) & 0x7;
		vdp.obj_ctrl.is_8bit = (value >> 14) & 0x1;
		break;
	case 0x012:
	case 0x014:
	{
		int index = (addr - 0x012) >> 1;
		Log::debug("[Video] write OBJ%d_PALSEL: %04X", index, value);
		vdp.obj_palsel[index] = value;
		break;
	}
	case 0x020:
		Log::debug("[Video] write TILEBASE: %04X", value);
		//Seven bits
		vdp.tilebase = value & 0x7F;
		break;
	default:
		LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
	}
}

void bgobj_write32(uint32_t addr, uint32_t value)
{
	WRITE_DOUBLEWORD(bgobj, addr, value);
}

uint8_t display_read8(uint32_t addr)
{
	READ_HALFWORD(display, addr);
}

uint16_t display_read16(uint32_t addr)
{
	addr &= 0xFFE;
	switch (addr)
	{
	case 0x000:
		return vdp.dispmode;
	case 0x002:
	{
		uint16_t result = 0;

		for (int i = 0; i < 2; i++)
		{
			result |= vdp.layer_ctrl.bg_enable[i] << i;
			result |= vdp.layer_ctrl.obj_enable[i] << (i + 6);
		}

		for (int i = 0; i < 4; i++)
		{
			result |= vdp.layer_ctrl.bitmap_enable[i] << (i + 2);
		}

		result |= vdp.layer_ctrl.bitmap_screen_mode[0] << 8;
		result |= vdp.layer_ctrl.bitmap_screen_mode[1] << 10;
		result |= vdp.layer_ctrl.obj_screen_mode[0] << 12;
		result |= vdp.layer_ctrl.obj_screen_mode[1] << 14;
		return result;
	}
	case 0x004:
	{
		uint16_t result = vdp.color_prio.prio_mode;
		result |= vdp.color_prio.screen_b_backdrop_only << 4;
		result |= vdp.color_prio.output_screen_b << 5;
		result |= vdp.color_prio.output_screen_a << 6;
		result |= vdp.color_prio.blend_mode << 7;
		return result;
	}
	case 0x006:
		//Note the reversed order!
		return vdp.backdrops[1];
	case 0x008:
		return vdp.backdrops[0];
	case 0x00A:
		//Twelve bits read back; bits 11-10 have no known effect
		return vdp.capture_ctrl.raw;
	default:
		LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
		return 0;
	}
}

uint32_t display_read32(uint32_t addr)
{
	READ_DOUBLEWORD(display, addr);
}

void display_write8(uint32_t addr, uint8_t value)
{
	WRITE_HALFWORD(display, addr, value);
}

void display_write16(uint32_t addr, uint16_t value)
{
	addr &= 0xFFE;
	switch (addr)
	{
	case 0x000:
		vdp.dispmode = value & 0x7;
		Log::debug("[Video] write DISPMODE: %04X", value);
		break;
	case 0x002:
		for (int i = 0; i < 2; i++)
		{
			vdp.layer_ctrl.bg_enable[i] = (value >> i) & 0x1;
			if (before_line_latch(BG_ENABLE_LATCH_HCOUNT) || vdp.vcount >= vdp.visible_scanlines)
			{
				vdp.bg_enable_line[i] = vdp.layer_ctrl.bg_enable[i];
			}
			vdp.layer_ctrl.obj_enable[i] = (value >> (i + 6)) & 0x1;
		}

		for (int i = 0; i < 4; i++)
		{
			vdp.layer_ctrl.bitmap_enable[i] = (value >> (i + 2)) & 0x1;
		}

		vdp.layer_ctrl.bitmap_screen_mode[0] = (value >> 8) & 0x3;
		vdp.layer_ctrl.bitmap_screen_mode[1] = (value >> 10) & 0x3;
		vdp.layer_ctrl.obj_screen_mode[0] = (value >> 12) & 0x3;
		vdp.layer_ctrl.obj_screen_mode[1] = value >> 14;
		Log::debug("[Video] write LAYER_CTRL: %04X", value);
		break;
	case 0x004:
		vdp.color_prio.prio_mode = value & 0xF;
		vdp.color_prio.screen_b_backdrop_only = (value >> 4) & 0x1;
		vdp.color_prio.output_screen_b = (value >> 5) & 0x1;
		vdp.color_prio.output_screen_a = (value >> 6) & 0x1;
		vdp.color_prio.blend_mode = (value >> 7) & 0x1;
		Log::debug("[Video] write COLORPRIO: %04X", value);
		break;
	case 0x006:
		//Note the reversed order!
		split_line_colors(BACKDROP_SPLIT_OFFSET_Q);
		//15-bit colours: bit 15 is not stored
		vdp.backdrops[1] = value & 0x7FFF;
		break;
	case 0x008:
		split_line_colors(BACKDROP_SPLIT_OFFSET_Q);
		vdp.backdrops[0] = value & 0x7FFF;
		break;
	case 0x00A:
		vdp.capture_ctrl.scanline = value & 0xFF;
		vdp.capture_ctrl.format = (value >> 8) & 0x3;
		vdp.capture_ctrl.raw = value & 0x0FFF;
		break;
	default:
		LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
	}
}

void display_write32(uint32_t addr, uint32_t value)
{
	WRITE_DOUBLEWORD(display, addr, value);
}

uint8_t irq_read8(uint32_t addr)
{
	READ_HALFWORD(irq, addr);
}

uint16_t irq_read16(uint32_t addr)
{
	addr &= 0xFFE;

	switch (addr)
	{
	case 0x002:
		return vdp.irq0_hcmp;
	case 0x004:
		return vdp.irq0_vcmp;
	default:
		LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
		return 0;
	}
}

uint32_t irq_read32(uint32_t addr)
{
	READ_DOUBLEWORD(irq, addr);
}

void irq_write8(uint32_t addr, uint8_t value)
{
	WRITE_HALFWORD(irq, addr, value);
}

void irq_write16(uint32_t addr, uint16_t value)
{
	addr &= 0xFFE;

	switch (addr)
	{
	case 0x000:
		vdp.cmp_irq_ctrl.irq0_enable = (value >> 1) & 0x1;
		vdp.cmp_irq_ctrl.nmi_enable = (value >> 2) & 0x1;
		vdp.cmp_irq_ctrl.use_vcmp = (value >> 5) & 0x1;
		vdp.cmp_irq_ctrl.irq0_enable2 = (value >> 7) & 0x1;
		vdp.cmp_irq_ctrl.irq2_enable = (value & 0x49) == 0x49;
		vdp.cmp_irq_ctrl.irq2_source = (value >> 4) & 0x1;
		Log::debug("[VDP] write CMP_IRQ_CTRL: %04X", value);
		schedule_irq0();
		break;
	case 0x002:
		vdp.irq0_hcmp = value & 0x1FF;
		schedule_irq0();
		break;
	case 0x004:
		vdp.irq0_vcmp = value & 0x1FF;
		break;
	}
}

void irq_write32(uint32_t addr, uint32_t value)
{
	WRITE_DOUBLEWORD(irq, addr, value);
}

uint8_t dma_ctrl_read8(uint32_t addr)
{
	READ_HALFWORD(dma_ctrl, addr);
}

bool bitmap_fast_access()
{
	return vdp.bm_mem_ctrl & 0x1;
}

uint16_t dma_ctrl_read16(uint32_t addr)
{
	addr &= 0xFFE;
	switch (addr)
	{
	case 0x002:
		return vdp.dma_mask;
	case 0x004:
		return vdp.dma_value;
	default:
		LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
		return 0;
	}
}

uint32_t dma_ctrl_read32(uint32_t addr)
{
	READ_DOUBLEWORD(dma_ctrl, addr);
}

void dma_ctrl_write8(uint32_t addr, uint8_t value)
{
	WRITE_HALFWORD(dma_ctrl, addr, value);
}

void dma_ctrl_write16(uint32_t addr, uint16_t value)
{
	addr &= 0xFFE;
	switch (addr)
	{
	case 0x000:
		Log::debug("[Video] write BM_MEM_CTRL: %04X", value);
		vdp.bm_mem_ctrl = value & 0x7;
		break;
	case 0x002:
		//TODO: what does bit 8 do? Seems to have no effect in HW tests at this time
		vdp.dma_mask = value & 0x1FF;
		break;
	case 0x004:
		vdp.dma_value = value & 0xFF;
		break;
	default:
		LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
	}
}

void dma_ctrl_write32(uint32_t addr, uint32_t value)
{
	WRITE_DOUBLEWORD(dma_ctrl, addr, value);
}

uint8_t dma_read8(uint32_t addr)
{
	LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
	return 0;
}

uint16_t dma_read16(uint32_t addr)
{
	LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
	return 0;
}

uint32_t dma_read32(uint32_t addr)
{
	LOG_UNEMULATED("[Video] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
	return 0;
}

void dma_write8(uint32_t addr, uint8_t value)
{
	WRITE_HALFWORD(dma, addr, value);
}

void dma_write16(uint32_t addr, uint16_t value)
{
	//Value written doesn't matter, it always triggers this
	//TODO: how long does this take? Is the CPU stalled?
	addr &= 0x3FE;

	int y = addr >> 1;
	for (int x = 0; x < DISPLAY_WIDTH; x++)
	{
		uint32_t addr = x + (y * DISPLAY_WIDTH);
		uint8_t data = vdp.bitmap[addr];
		data &= ~vdp.dma_mask;
		data |= vdp.dma_value & vdp.dma_mask;
		vdp.bitmap[addr] = data;
	}
}

void dma_write32(uint32_t addr, uint32_t value)
{
	WRITE_DOUBLEWORD(dma, addr, value);
}

uint8_t debug_read8(uint32_t addr)
{
	READ_HALFWORD(debug, addr);
}

uint16_t debug_read16(uint32_t addr)
{
	//Write-only registers
	return 0;
}

uint32_t debug_read32(uint32_t addr)
{
	READ_DOUBLEWORD(debug, addr);
}

void debug_write8(uint32_t addr, uint8_t value)
{
	WRITE_HALFWORD(debug, addr, value);
}

void debug_write16(uint32_t addr, uint16_t value)
{
	//SYNC_CALIBRATE only moves the analog sync pulses, and the two debug registers
	//halt or blank the raster for hardware bring-up; none of it has a place here
}

void debug_write32(uint32_t addr, uint32_t value)
{
	WRITE_DOUBLEWORD(debug, addr, value);
}

}  // namespace Video