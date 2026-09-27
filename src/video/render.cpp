#include "video/render.h"

#include <common/bswp.h>
#include <log/log.h>

#include <algorithm>
#include <cassert>
#include <cstring>

#include "video/vdp_local.h"

namespace Video::Renderer
{

struct TilemapInfo
{
	int width;
	int height;
	uint32_t bg1_start;
	uint32_t data_start;
};

static void refresh_palette_cache()
{
	if (!vdp.palette_dirty)
	{
		return;
	}
	vdp.palette_dirty = false;

	for (int i = 0; i < 256; i++)
	{
		uint16_t color;
		memcpy(&color, vdp.palette + (i * 2), 2);
		vdp.palette_cache[i] = Common::bswp16(color);
	}
}

static uint16_t read_palette(uint8_t value)
{
	return vdp.palette_cache[value];
}

static uint16_t read_screen(int index, int x)
{
	uint8_t pal_color = vdp.screens[index][x];
	if (!pal_color || (index == 1 && vdp.color_prio.screen_b_backdrop_only))
	{
		return vdp.backdrops[index];
	}

	return read_palette(pal_color);
}

static void write_screen(int index, int x, uint8_t value)
{
	x &= 0x1FF;
	if (x < DISPLAY_WIDTH)
	{
		vdp.screens[index][x] = value;
	}
}

static inline void write_color_raw(std::unique_ptr<uint16_t[]>& buffer, int x, int y, uint16_t value)
{
	x &= 0x1FF;
	if (x < DISPLAY_WIDTH)
	{
		buffer[x + (y * DISPLAY_WIDTH)] = value;
	}
}

//Final encoding of a pixel in the display buffer. The buffer is handed straight
//to the frontend, so it is produced in RGB565 here - at the one point each pixel
//is written - rather than converting the whole frame again in a separate pass.
//The VDP's own colors are 15bpp, so green gains a bit, replicating its top bit.
static inline uint16_t display_encode(uint16_t color)
{
	uint16_t r = (color >> 10) & 0x1F;
	uint16_t g = (color >> 5) & 0x1F;
	uint16_t b = color & 0x1F;
	return (uint16_t)((r << 11) | (g << 6) | ((g >> 4) << 5) | b);
}

static inline void write_display_color(int x, int y, uint16_t color)
{
	write_color_raw(vdp.display_output, x, y, display_encode(color));
}

static int get_bg_tile_size(int index)
{
	int tile_size = (index == 0) ? vdp.bg_ctrl.tile_size0 : vdp.bg_ctrl.tile_size1;
	switch (tile_size)
	{
	case 0x00:
		tile_size = 8;
		break;
	case 0x01:
		tile_size = 16;
		break;
	case 0x02:
		tile_size = 32;
		break;
	case 0x03:
		tile_size = 64;
		break;
	default:
		assert(0);
	}
	return tile_size;
}

static void get_tilemap_info(TilemapInfo& info)
{
	switch (vdp.bg_ctrl.map_size)
	{
	case 0x00:
		info.width = 64;
		info.height = 64;
		break;
	case 0x01:
		info.width = 64;
		info.height = 32;
		break;
	case 0x02:
		info.width = 32;
		info.height = 64;
		break;
	case 0x03:
		info.width = 32;
		info.height = 32;
		break;
	default:
		assert(0);
	}

	info.data_start = (info.width * info.height) << 1;
	if (vdp.bg_ctrl.shared_maps)
	{
		info.bg1_start = 0;
	}
	else
	{
		info.bg1_start = info.data_start;
		info.data_start <<= 1;
	}
}

//Offset of one pixel of a tile or object in character space, in 8bpp pixels (halve
//it for 4bpp). Characters form a grid 8 wide, and a tile or object larger than one
//character wraps on each axis on its own: a 16x16 tile at character 7 uses 7, 0, 15
//and 8. Split into row | column parts so the row part is computed once per line.
static inline uint32_t char_row_offset(int chr, int split_rows, int tile_y)
{
	int row = ((chr >> 3) + split_rows + (tile_y >> 3)) & 0xFF;
	return ((uint32_t)row << 9) | ((tile_y & 0x7) << 3);
}

static inline uint32_t char_col_offset(int chr, int tile_x)
{
	return (uint32_t)(((chr + (tile_x >> 3)) & 0x7) << 6) | (tile_x & 0x7);
}

static void draw_bg(int index, int screen_y)
{
	if (!vdp.bg_enable_line[index])
	{
		return;
	}

	bool is_8bit = index == 0 && vdp.bg_ctrl.bg0_8bit;
	int tile_size = get_bg_tile_size(index);
	int tile_size_mask = tile_size - 1;

	//tile_size is a power of two: shift instead of dividing per pixel
	int tile_shift = 3;
	while ((1 << tile_shift) < tile_size)
	{
		tile_shift++;
	}

	TilemapInfo tilemap;
	get_tilemap_info(tilemap);

	uint32_t map_start = (index == 1) ? tilemap.bg1_start : 0;

	//4bpp characters start after the 8bpp rows the character split reserves. The
	//split counts in character-grid rows, two per reserved 8bpp row.
	int split_rows = is_8bit ? 0 : (vdp.tilebase & 0x7F) << 1;

	//The vertical position and its derived tilemap row are constant across
	//the scanline; the tile descriptor only changes at tile boundaries
	int scrollx = vdp.bg_scrollx_line[index];
	int wrap_x_mask = (tilemap.width * tile_size) - 1;
	int wrap_y_mask = (tilemap.height * tile_size) - 1;
	//Scroll Y written during the line takes over from its split pixel on
	int splits = vdp.bg_y_split_count[index], next_split = 0;
	int next_split_x = splits ? vdp.bg_y_split_x[index][0] : DISPLAY_WIDTH;
	int y = (screen_y + (splits ? vdp.bg_scrolly_start[index] : vdp.bg_scrolly[index])) & wrap_y_mask;
	uint32_t map_row = (uint32_t)(y >> tile_shift) * tilemap.width;

	uint32_t last_map_offs = 0xFFFFFFFF;
	uint16_t descriptor = 0;
	uint16_t tile_index = 0;
	int screen_index = 0;
	int pal_descriptor = 0;
	bool x_flip = false;
	bool y_flip = false;
	uint32_t char_row_bits = 0;

	for (int screen_x = 0; screen_x < DISPLAY_WIDTH; screen_x++)
	{
		while (screen_x >= next_split_x)
		{
			y = (screen_y + vdp.bg_y_split_value[index][next_split]) & wrap_y_mask;
			map_row = (uint32_t)(y >> tile_shift) * tilemap.width;
			last_map_offs = 0xFFFFFFFF;
			next_split++;
			next_split_x = next_split < splits ? vdp.bg_y_split_x[index][next_split] : DISPLAY_WIDTH;
		}
		int x = (screen_x + scrollx) & wrap_x_mask;

		uint32_t map_offs = (uint32_t)(x >> tile_shift) + map_row;
		if (map_offs != last_map_offs)
		{
			last_map_offs = map_offs;

			memcpy(&descriptor, &vdp.tile[map_start + (map_offs << 1)], 2);
			descriptor = Common::bswp16(descriptor);

			tile_index = descriptor & 0x7FF;
			screen_index = (descriptor >> 11) & 0x1;
			pal_descriptor = (descriptor >> 12) & 0x3;
			x_flip = (descriptor >> 14) & 0x1;
			y_flip = descriptor >> 15;

			int tile_y = y & tile_size_mask;
			if (y_flip)
			{
				tile_y = tile_size_mask - tile_y;
			}
			char_row_bits = char_row_offset(tile_index, split_rows, tile_y);
		}

		int tile_x = x & tile_size_mask;
		if (x_flip)
		{
			tile_x = tile_size_mask - tile_x;
		}

		uint32_t offs = char_row_bits | char_col_offset(tile_index, tile_x);

		uint8_t tile_data;
		if (is_8bit)
		{
			tile_data = vdp.tile[(tilemap.data_start + (offs & 0xFFFF)) & 0xFFFF];
		}
		else
		{
			tile_data = vdp.tile[(tilemap.data_start + (offs >> 1)) & 0xFFFF];
			if (tile_x & 0x1)
			{
				tile_data &= 0xF;
			}
			else
			{
				tile_data >>= 4;
			}
		}

		//0 is transparent, no matter if it's 4-bit or 8-bit
		if (!tile_data)
		{
			continue;
		}

		uint8_t output = tile_data;
		if (!is_8bit)
		{
			uint16_t palsel = vdp.bg_palsel[index];
			int pal = (palsel >> (pal_descriptor * 4)) & 0xF;
			output |= pal << 4;
		}

		write_screen(screen_index, screen_x, output);
	}
}

static void draw_bitmap(int index, int y)
{
	if (!vdp.layer_ctrl.bitmap_enable_frame[index])
	{
		return;
	}

	VDP::BitmapRegs* regs = &vdp.bitmap_regs[index];

	//Skip drawing if the bitmap is off-screen verticaly
	if (((y - regs->screeny) & 0x1FF) > regs->h)
	{
		return;
	}

	int screenx = regs->frame_screenx;
	if (screenx & 0x100)
	{
		screenx -= 0x200;
	}
	int visible_left = std::max(0, screenx + regs->clipx);
	int visible_right = std::min(255, screenx + regs->w);

	//Skip drawing if the bitmap is off-screen horizontally
	if (visible_left > 255 || visible_right < 0)
	{
		return;
	}

	bool is_8bit = false;
	bool split_x = false, split_y = false;
	int vram_width = 0, vram_height = 0;

	//Modes 5-7 are invalid: 5 behaves as 0, and 6 and 7 as 1
	int bitmap_mode = vdp.bitmap_ctrl & 0x7;
	if (bitmap_mode == 5)
	{
		bitmap_mode = 0;
	}
	else if (bitmap_mode > 5)
	{
		bitmap_mode = 1;
	}

	switch (bitmap_mode)
	{
	case 0x00:
		is_8bit = true;
		split_y = true;
		vram_width = 256;
		vram_height = 256;
		break;
	case 0x01:
		is_8bit = true;
		vram_width = 256;
		vram_height = 512;
		break;
	case 0x02:
		split_y = true;
		vram_width = 512;
		vram_height = 256;
		break;
	case 0x03:
		split_x = true;
		vram_width = 256;
		vram_height = 512;
		break;
	case 0x04:
		vram_width = 512;
		vram_height = 512;
		break;
	default:
		assert(0);
	}
	uint8_t subpalette_bits = ((vdp.bitmap_palsel >> ((3 - index) * 4)) & 0xF) << 4;
	bool use_color_buffer = (regs->buffer_ctrl & 0x100) != 0;

	int output_mode = vdp.layer_ctrl.bitmap_screen_mode[index >> 1];
	//A layer feeding neither screen has no visible effect; the buffered color
	//is the only other state the line fetch mutates
	if (!output_mode && !use_color_buffer)
	{
		return;
	}

	int width_mask = vram_width - 1;
	int height_mask = vram_height - 1;

	int data_y = (y + regs->frame_scrolly - regs->screeny) & height_mask;
	//If split_y is true, there are two separate maps at y=0 and y=256 that get scrolled independently
	if (split_y)
	{
		data_y |= regs->frame_scrolly & 0x100;
	}

	//Fetch the appropriate line independent of screenx and process color buffering
	//TODO: this fetching should take place on the previous scanline (should subpalette mapping happen earlier too?)
	//Loop invariants live in locals: the byte stores below would otherwise
	//force the compiler to re-read them from vdp every pixel.
	//Every mode's VRAM row is 256 bytes (4-bit modes pack two pixels per
	//byte; split_x moves its map-select bit into the row via data_x).
	uint8_t bm_cache_line[256];
	int bm_cache_end = std::min(255, regs->w + 1); //HW bug: one extra pixel is processed unless full line
	int scrollx = regs->line_scrollx;
	int split_x_bits = split_x ? (scrollx & 0x100) : 0;
	uint32_t row_base = (uint32_t)data_y << 8;

	if (is_8bit)
	{
		if (!use_color_buffer)
		{
			//No per-pixel transform: copy the line in contiguous runs,
			//splitting only at the horizontal wrap
			int x = 0;
			while (x <= bm_cache_end)
			{
				int data_x = (x + scrollx) & width_mask;
				int run = std::min(bm_cache_end + 1 - x, vram_width - data_x);
				memcpy(bm_cache_line + x, &vdp.bitmap[(data_x + row_base) & 0x1FFFF], run);
				x += run;
			}
		}
		else
		{
			uint8_t buffered_color = regs->buffered_color;
			uint8_t threshold = regs->buffer_ctrl & 0xFF;
			for (int x = 0; x <= bm_cache_end; x++)
			{
				int data_x = (x + scrollx) & width_mask;
				uint8_t data = vdp.bitmap[(data_x + row_base) & 0x1FFFF];
				if (data == 0xFF)
				{
					//HW bug: 0xFF fails to get replaced if x=0xFF
					if (x != 0xFF)
					{
						data = buffered_color;
					}
				}
				else if (data < threshold)
				{
					buffered_color = data;
				}
				bm_cache_line[x] = data;
			}
			regs->buffered_color = buffered_color;
		}
	}
	else if (!use_color_buffer)
	{
		for (int x = 0; x <= bm_cache_end; x++)
		{
			int data_x = ((x + scrollx) & width_mask) | split_x_bits;
			uint8_t data = vdp.bitmap[((data_x >> 1) + row_base) & 0x1FFFF];
			//Even x holds the high nibble
			data = (data >> (((data_x & 0x1) ^ 0x1) << 2)) & 0xF;
			if (data)
			{
				data |= subpalette_bits;
			}
			bm_cache_line[x] = data;
		}
	}
	else
	{
		uint8_t buffered_color = regs->buffered_color;
		uint8_t threshold = regs->buffer_ctrl & 0x0F;
		for (int x = 0; x <= bm_cache_end; x++)
		{
			int data_x = ((x + scrollx) & width_mask) | split_x_bits;
			uint8_t data = vdp.bitmap[((data_x >> 1) + row_base) & 0x1FFFF];
			data = (data >> (((data_x & 0x1) ^ 0x1) << 2)) & 0xF;
			if (data)
			{
				//0xF marks "use the buffered color" in this mode
				data = (data == 0xF) ? 0xFF : (data | subpalette_bits);
			}
			if (data == 0xFF)
			{
				//HW bug: 0xFF fails to get replaced if x=0xFF
				if (x != 0xFF)
				{
					data = buffered_color;
				}
			}
			else if ((data & 0x0F) < threshold)
			{
				buffered_color = data;
			}
			bm_cache_line[x] = data;
		}
		regs->buffered_color = buffered_color;
	}

	//Now draw the appropriate part of the cache line to the screen according to screenx
	//visible_left/right are already clamped to [0, 255], so the screens can be
	//written directly without write_screen's wrap/bounds handling
	uint8_t* screen_a = vdp.screens[0];
	uint8_t* screen_b = vdp.screens[1];
	for (int x = visible_left; x <= visible_right; x++)
	{
		uint8_t data = bm_cache_line[(x - screenx) & 0xFF];
		if (data == 0)
		{
			continue;
		}

		if (output_mode & 0x1)
		{
			screen_b[x] = data;
		}

		if (output_mode & 0x2)
		{
			screen_a[x] = data;
		}
	}
}

//Object dimensions by the descriptor's 2-bit size field
constexpr static int OBJ_SIZE_W[4] = {8, 16, 16, 32};
constexpr static int OBJ_SIZE_H[4] = {8, 16, 32, 32};

//Decoded OAM. Every scanline used to re-read and byte-swap all 128 descriptors
//for each of the two object layers - ~61k decodes a frame, the bulk of the
//object cost - even though OAM normally changes only once per frame. The decode
//now happens when OAM actually changes, and the scanline pass walks a prebuilt
//per-layer id list (kept in descending id order, since OBJ #0 has the highest
//priority and must be drawn last).
struct ObjCacheEntry
{
	uint32_t descriptor;
	int16_t start_x;
	int16_t start_y;
	int16_t end_y;
	uint8_t width;
	uint8_t height;
};

static ObjCacheEntry obj_cache[OBJ_COUNT];
static uint8_t obj_list[2][OBJ_COUNT];
static int obj_list_count[2];
static int obj_cache_id_offs = -1;

static void refresh_obj_cache()
{
	//id_offs rotates which layer an object belongs to, so a change to it
	//repartitions the lists just as an OAM write does
	if (!vdp.oam_dirty && obj_cache_id_offs == vdp.obj_ctrl.id_offs)
	{
		return;
	}
	vdp.oam_dirty = false;
	obj_cache_id_offs = vdp.obj_ctrl.id_offs;

	obj_list_count[0] = 0;
	obj_list_count[1] = 0;

	for (int id = OBJ_COUNT - 1; id >= 0; id--)
	{
		uint32_t descriptor;
		memcpy(&descriptor, vdp.oam + (id * 4), 4);
		descriptor = Common::bswp32(descriptor);

		int tile_size = (descriptor >> 10) & 0x3;
		int obj_height = OBJ_SIZE_H[tile_size];

		int start_y = (descriptor >> 16) & 0xFF;
		start_y |= ((descriptor >> 9) & 0x1) << 8;

		ObjCacheEntry& entry = obj_cache[id];
		entry.descriptor = descriptor;
		entry.start_x = (int16_t)(descriptor & 0x1FF);
		entry.start_y = (int16_t)start_y;
		entry.end_y = (int16_t)((start_y + obj_height) & 0x1FF);
		entry.width = (uint8_t)OBJ_SIZE_W[tile_size];
		entry.height = (uint8_t)obj_height;

		int test_id = (id - obj_cache_id_offs) & 0xFF;
		int layer = (test_id < OBJ_COUNT) ? 0 : 1;
		obj_list[layer][obj_list_count[layer]++] = (uint8_t)id;
	}
}

static void draw_obj(int index, int screen_y)
{
	if (!vdp.layer_ctrl.obj_enable[index])
	{
		return;
	}

	//TODO: limit the maximum number of sprites per scanline

	int output_mode = vdp.layer_ctrl.obj_screen_mode[index];

	//With the debug layer buffers compiled out, a layer feeding neither screen
	//has no observable effect at all
	if (!output_mode)
	{
		return;
	}

	refresh_obj_cache();

	//Tilemap info is only useful here to get the start of tile data
	TilemapInfo tilemap;
	get_tilemap_info(tilemap);

	//Invariant for every object on this layer
	bool is_8bit = vdp.obj_ctrl.is_8bit;
	uint32_t data_start = tilemap.data_start;
	int split_rows = is_8bit ? 0 : (vdp.tilebase & 0x7F) << 1;
	int tile_index_offs = vdp.obj_ctrl.tile_index_offs[index] << 8;
	uint16_t palsel = vdp.obj_palsel[index];
	const uint8_t* tile_mem = vdp.tile;
	uint8_t* screen_b = vdp.screens[1];
	uint8_t* screen_a = vdp.screens[0];

	const uint8_t* ids = obj_list[index];
	int id_count = obj_list_count[index];

	for (int i = 0; i < id_count; i++)
	{
		const ObjCacheEntry& entry = obj_cache[ids[i]];

		int start_y = entry.start_y;
		int end_y = entry.end_y;

		if (end_y > start_y)
		{
			if (screen_y < start_y || screen_y >= end_y)
			{
				continue;
			}
		}
		else
		{
			if (screen_y < start_y && screen_y >= end_y)
			{
				continue;
			}
		}

		uint32_t descriptor = entry.descriptor;
		int start_x = entry.start_x;
		int obj_width = entry.width;
		int obj_height = entry.height;

		//Everything below is constant for this object on this scanline, so it is
		//computed once rather than per pixel
		bool x_flip = (descriptor >> 14) & 0x1;
		bool y_flip = (descriptor >> 15) & 0x1;

		int tile_y = (screen_y - start_y) & (obj_height - 1);
		if (y_flip)
		{
			tile_y = obj_height - 1 - tile_y;
		}

		//The full 11-bit starting character: the layer's high bits over the entry's 8
		int chr = (int)(descriptor >> 24) | tile_index_offs;
		uint32_t row_bits = char_row_offset(chr, split_rows, tile_y);

		if (is_8bit)
		{
			for (int px = 0; px < obj_width; px++)
			{
				int screen_x = (start_x + px) & 0x1FF;
				if (screen_x >= DISPLAY_WIDTH)
				{
					continue;
				}

				int tile_x = x_flip ? (obj_width - 1 - px) : px;

				uint32_t offs = row_bits | char_col_offset(chr, tile_x);
				uint8_t output = tile_mem[(data_start + (offs & 0xFFFF)) & 0xFFFF];
				if (!output)
				{
					continue;
				}

				if (output_mode & 0x1)
				{
					screen_b[screen_x] = output;
				}

				if (output_mode & 0x2)
				{
					screen_a[screen_x] = output;
				}
			}
		}
		else
		{
			//4bpp: the palette high nibble is per-object, not per-pixel
			int pal_descriptor = (descriptor >> 12) & 0x3;
			uint8_t pal_bits = (uint8_t)(((palsel >> (pal_descriptor * 4)) & 0xF) << 4);

			for (int px = 0; px < obj_width; px++)
			{
				int screen_x = (start_x + px) & 0x1FF;
				if (screen_x >= DISPLAY_WIDTH)
				{
					continue;
				}

				int tile_x = x_flip ? (obj_width - 1 - px) : px;

				uint32_t offs = (row_bits | char_col_offset(chr, tile_x)) >> 1;
				uint8_t tile_data = tile_mem[(data_start + offs) & 0xFFFF];
				tile_data = (tile_x & 0x1) ? (tile_data & 0xF) : (tile_data >> 4);

				if (!tile_data)
				{
					continue;
				}

				uint8_t output = tile_data | pal_bits;

				if (output_mode & 0x1)
				{
					screen_b[screen_x] = output;
				}

				if (output_mode & 0x2)
				{
					screen_a[screen_x] = output;
				}
			}
		}
	}
}

static void draw_layers(int y)
{
	//Draw each layer
	//The order is important - each layer has a different priority, and lower priority layers are drawn first here
	int bitmap_prio = vdp.color_prio.prio_mode & 0x1;
	int bg0_prio = (vdp.color_prio.prio_mode >> 1) & 0x1;
	int obj0_prio = vdp.color_prio.prio_mode >> 2;

	int bitmap_low = (bitmap_prio == 1) ? 0 : 2;
	int bitmap_hi = (bitmap_low + 2) & 0x3;

	if (obj0_prio == 3)
	{
		draw_obj(0, y);
	}

	draw_bg(1, y);

	if (!bg0_prio)
	{
		draw_bg(0, y);
	}

	if (obj0_prio == 2)
	{
		draw_obj(0, y);
	}

	draw_bitmap(bitmap_low + 1, y);
	draw_bitmap(bitmap_low, y);

	if (obj0_prio == 1)
	{
		draw_obj(0, y);
	}

	draw_bitmap(bitmap_hi + 1, y);
	draw_bitmap(bitmap_hi, y);

	if (bg0_prio)
	{
		draw_bg(0, y);
	}

	draw_obj(1, y);

	if (obj0_prio == 0)
	{
		draw_obj(0, y);
	}
}

//---------------------------------------------------------------------------
//The blend stage: how screens A and B become the picture (VDP.BLEND_MODE).
//
//Each mode has its own loop in draw_scanline, built from the per-pixel helpers
//below. Capture format 0 uses the same helpers (blended_pixel), so a capture always
//matches the display.
//---------------------------------------------------------------------------

//Features no retail game is known to use, reported once per session
enum RareFeature
{
	RARE_BLEND_A_ONLY,
	RARE_BLEND_HIRES,
	RARE_BLEND_INVALID,
	RARE_MODE4_TOP_OFF,
	RARE_MODE4_SBCOL,
	RARE_MODE5_TOP_OFF,
	RARE_CAPTURE_BLENDED,
};

static uint32_t rare_reported;

static void report_once(RareFeature feature, const char* message)
{
	if (rare_reported & (1u << feature))
	{
		return;
	}
	rare_reported |= 1u << feature;
	Log::info("[Video] %s", message);
}

void reset()
{
	rare_reported = 0;
}

//A screen's colour entering the blend: its layers, or its backdrop where they are
//transparent. A screen switched off at SCREEN_CTRL is black, which is how modes
//0-3 treat it. Modes 4 and 5 treat an off top screen as transparent instead, and
//check for that themselves.
static inline uint16_t screen_a_color(int x)
{
	return vdp.color_prio.output_screen_a ? read_screen(0, x) : 0;
}

static inline uint16_t screen_b_color(int x)
{
	return vdp.color_prio.output_screen_b ? read_screen(1, x) : 0;
}

//Modes 0 and 1: per-channel add or subtract (CSUB), optionally halved, clamped
static inline uint16_t color_math_pixel(int x, bool half)
{
	uint16_t input_a = screen_a_color(x);
	uint16_t input_b = screen_b_color(x);

	int a_r = (input_a >> 10) & 0x1F;
	int a_g = (input_a >> 5) & 0x1F;
	int a_b = input_a & 0x1F;

	int b_r = (input_b >> 10) & 0x1F;
	int b_g = (input_b >> 5) & 0x1F;
	int b_b = input_b & 0x1F;

	int out_r, out_g, out_b;

	if (vdp.color_prio.blend_mode)
	{
		//Subtractive blending
		out_r = a_r - b_r;
		out_g = a_g - b_g;
		out_b = a_b - b_b;
	}
	else
	{
		//Additive blending
		out_r = a_r + b_r;
		out_g = a_g + b_g;
		out_b = a_b + b_b;
	}

	if (half)
	{
		out_r >>= 1;
		out_g >>= 1;
		out_b >>= 1;
	}

	out_r = std::clamp(out_r, 0, 0x1F);
	out_g = std::clamp(out_g, 0, 0x1F);
	out_b = std::clamp(out_b, 0, 0x1F);

	return (uint16_t)((out_r << 10) | (out_g << 5) | out_b);
}

//Modes 4 (B over A) and 5 (A over B). The top screen shows only where it drew a
//layer; where it is transparent (or switched off) the bottom one shows, or black if
//that is off. In mode 4 an SBCOL screen B covers A with its backdrop everywhere.
static inline uint16_t overlay_pixel(int x, bool screen_b_on_top)
{
	if (screen_b_on_top)
	{
		if (vdp.color_prio.output_screen_b && vdp.color_prio.screen_b_backdrop_only)
		{
			return vdp.backdrops[1];
		}
		uint8_t b = vdp.screens[1][x];
		bool b_drew = b && vdp.color_prio.output_screen_b && !vdp.color_prio.screen_b_backdrop_only;
		return b_drew ? read_palette(b) : screen_a_color(x);
	}

	uint8_t a = vdp.screens[0][x];
	bool a_drew = a && vdp.color_prio.output_screen_a;
	return a_drew ? read_palette(a) : screen_b_color(x);
}

//The blend result for one pixel in 15bpp: what the display shows, and what
//capture format 0 records. Hi-res has no single blended colour; the megadoc
//defines its "blended output" as screen B alone.
static uint16_t blended_pixel(int x)
{
	switch (vdp.dispmode)
	{
	case 0x00:
		return color_math_pixel(x, false);
	case 0x01:
		return color_math_pixel(x, true);
	case 0x02:
		return screen_a_color(x);
	case 0x03:
		return screen_b_color(x);
	case 0x04:
		return overlay_pixel(x, true);
	case 0x05:
		return overlay_pixel(x, false);
	default:
		return 0;
	}
}

//The pixels of the line being composited: all of it, or one part of it when a
//palette or backdrop write split the line (see draw_scanline)
static int seg_x0 = 0;
static int seg_x1 = DISPLAY_WIDTH;

static void draw_color_math(int y, bool half)
{
	for (int x = seg_x0; x < seg_x1; x++)
	{
		write_display_color(x, y, color_math_pixel(x, half));
	}
}

static void draw_screen_overlay(int y, bool screen_b_on_top)
{
	for (int x = seg_x0; x < seg_x1; x++)
	{
		write_display_color(x, y, overlay_pixel(x, screen_b_on_top));
	}
}

//Mode 2: screen A alone, screen B unused
static void draw_screen_a_only(int y)
{
	for (int x = seg_x0; x < seg_x1; x++)
	{
		write_display_color(x, y, screen_a_color(x));
	}
}

//Mode 3, hi-res: each pixel is two half-pixels, screen A then B, written into the
//512-wide buffer (see compose_hires_frame). The 256-wide row keeps screen B, the
//mode's "blended output".
static void draw_hires(int y)
{
	uint16_t* row = vdp.display_output_hires.get() + (size_t)y * HIRES_DISPLAY_WIDTH;
	for (int x = seg_x0; x < seg_x1; x++)
	{
		uint16_t a = screen_a_color(x);
		uint16_t b = screen_b_color(x);
		row[x * 2] = display_encode(a);
		row[x * 2 + 1] = display_encode(b);
		write_display_color(x, y, b);
	}
	vdp.line_is_hires[y] = 1;
	vdp.frame_has_hires = true;
}

//Modes 6 and 7 are invalid; the megadoc reports "generally a black screen"
static void draw_invalid_blend(int y)
{
	for (int x = seg_x0; x < seg_x1; x++)
	{
		write_display_color(x, y, 0);
	}
}

static void display_capture(int y)
{
	uint16_t* capture_buffer_15bpp = (uint16_t*)&vdp.capture_buffer[0];
	switch (vdp.capture_ctrl.format)
	{
	case 0:
		//The blended output as displayed, backdrops included, in 15bpp (screen B
		//alone in hi-res), from the same helpers as the display
		report_once(RARE_CAPTURE_BLENDED, "scanline capture of the blended output (format 0) in use");
		for (int x = seg_x0; x < seg_x1; x++)
		{
			capture_buffer_15bpp[x] = Common::bswp16(blended_pixel(x) & 0x7FFF);
		}
		break;
	case 1:
		//Capture screen A in 15bpp via the palette/backdrop
		for (int x = seg_x0; x < seg_x1; x++)
		{
			capture_buffer_15bpp[x] = Common::bswp16(read_screen(0, x));
		}
		break;
	case 2:
	case 3:
		//Capture screen A in 8bpp
		memcpy(vdp.capture_buffer + seg_x0, vdp.screens[0] + seg_x0, (size_t)(seg_x1 - seg_x0));
		break;
	default:
		assert(0);
	}
}

static void compose_line(int y);

void draw_scanline(int y)
{
	refresh_palette_cache();

	//Set both screens to the backdrop color
	memset(vdp.screens, 0, sizeof(vdp.screens));

	draw_layers(y);

	if (!vdp.color_split_count)
	{
		compose_line(y);
		return;
	}

	//A palette or backdrop write while the line was drawn: each part of the line
	//before it takes the colours in force until then
	uint16_t palette_now[PALETTE_SIZE / 2];
	uint16_t backdrops_now[2] = { vdp.backdrops[0], vdp.backdrops[1] };
	memcpy(palette_now, vdp.palette_cache, sizeof(palette_now));
	for (int i = 0; i < vdp.color_split_count; i++)
	{
		const auto& split = vdp.color_splits[i];
		memcpy(vdp.palette_cache, split.palette, sizeof(palette_now));
		vdp.backdrops[0] = split.backdrops[0];
		vdp.backdrops[1] = split.backdrops[1];
		seg_x1 = split.x_end;
		compose_line(y);
		seg_x0 = seg_x1;
	}
	memcpy(vdp.palette_cache, palette_now, sizeof(palette_now));
	vdp.backdrops[0] = backdrops_now[0];
	vdp.backdrops[1] = backdrops_now[1];
	seg_x1 = DISPLAY_WIDTH;
	compose_line(y);
	seg_x0 = 0;
}

//Combine the screens into the display output buffer, for the pixels seg_x0 to
//seg_x1, and capture them if this is the capture line
static void compose_line(int y)
{
	//Combine the screens into the display output buffer
	switch (vdp.dispmode)
	{
	case 0x00:
		draw_color_math(y, false);
		break;
	case 0x01:
		draw_color_math(y, true);
		break;
	case 0x02:
		report_once(RARE_BLEND_A_ONLY, "blend mode 2 (screen A only) in use");
		draw_screen_a_only(y);
		break;
	case 0x03:
		report_once(RARE_BLEND_HIRES, "blend mode 3 (hi-res) in use: frames containing it are output 512 pixels wide");
		draw_hires(y);
		break;
	case 0x04:
		if (!vdp.color_prio.output_screen_b)
		{
			report_once(RARE_MODE4_TOP_OFF, "blend mode 4 with screen B off: B shows as transparent");
		}
		else if (vdp.color_prio.screen_b_backdrop_only)
		{
			report_once(RARE_MODE4_SBCOL, "blend mode 4 with SBCOL: screen B covers A with its backdrop colour");
		}
		draw_screen_overlay(y, true);
		break;
	case 0x05:
		if (!vdp.color_prio.output_screen_a)
		{
			report_once(RARE_MODE5_TOP_OFF, "blend mode 5 with screen A off: A shows as transparent");
		}
		draw_screen_overlay(y, false);
		break;
	default:
		report_once(RARE_BLEND_INVALID, "invalid blend mode (6 or 7) in use: picture is black");
		draw_invalid_blend(y);
		break;
	}

	if (vdp.capture_enable && y == vdp.capture_ctrl.scanline)
	{
		display_capture(y);
		if (seg_x1 == DISPLAY_WIDTH)
		{
			vdp.capture_enable = false;
		}
	}
}

}  // namespace Video::Renderer