#include "core/sh2/sh2_bus.h"

#include <common/bswp.h>
#include <log/log.h>
#include <sound/sound.h>
#include <video/video.h>

#include <cstdio>
#include <cstring>
#include <fstream>

#include "core/loopy_io.h"
#include "core/sh2/peripherals/sh2_ocpm.h"
#include "core/sh2/sh2_local.h"
#include "expansion/expansion.h"

namespace SH2::Bus
{

static uint32_t translate_addr(uint32_t addr)
{
	//Bits 28-31 are always ignored
	//The on-chip region (bits 24-27 == 0xF) is NOT mirrored - all other regions are mirrored
	if ((addr & 0x0F000000) != 0x0F000000)
	{
		return addr & ~0xF8000000;
	}

	return addr & ~0xF0000000;
}

//Reads that the idle-loop detector is allowed to consider repeatable. Page-table
//memory (RAM/ROM/SRAM) qualifies implicitly - it changes only when something
//writes it. Of the MMIO regions only the VDP control registers qualify: they
//hold VCOUNT/HCOUNT, which advance solely inside scheduler events, and that is
//exactly what the vblank wait loops poll. Everything else is treated as unsafe,
//most importantly the on-chip peripherals, where reading the free-running timer
//counter derives its value from the live timestamp (sh2_timers.cpp) and so does
//change mid-slice, and where a read can clear a status flag.
static inline bool is_repeatable_read(uint32_t addr)
{
	//HCOUNT (0x002) is live: it advances with time inside a slice, so a loop
	//polling it is not idle
	return addr >= Video::CTRL_REG_START && addr < Video::CTRL_REG_END &&
		(addr & 0xFFE) != 0x002;
}

//A CPU read of a time-dependent register (HCOUNT, VCOUNT, TCNT, the ITU flags)
//returns the state at the time the bus model has reached when the device is
//called, except a VDP read from external-bus code, which samples a cycle before
//its access ends (measured)
struct ReadBias
{
	ReadBias(uint32_t addr)
	{
		if (sh2.in_execute && sh2.exec_external && (addr >> 24) == 0x4)
		{
			Timing::read_bias = -1;
		}
	}
	~ReadBias() { Timing::read_bias = 0; }
};

#define MMIO_ACCESS(access, ...)                                                                                      \
	if (addr >= OCPM::ORAM_BASE_ADDR && addr < OCPM::ORAM_END_ADDR) return OCPM::oram_##access(__VA_ARGS__);          \
	if (addr >= Video::PALETTE_START && addr < Video::PALETTE_END) return Video::palette_##access(__VA_ARGS__);       \
	if (addr >= Video::OAM_START && addr < Video::OAM_END) return Video::oam_##access(__VA_ARGS__);                   \
	if (addr >= Video::CAPTURE_START && addr < Video::CAPTURE_END) return Video::capture_##access(__VA_ARGS__);       \
	if (addr >= Video::CTRL_REG_START && addr < Video::CTRL_REG_END) return Video::ctrl_##access(__VA_ARGS__);        \
	if (addr >= Video::BITMAP_REG_START && addr < Video::BITMAP_REG_END)                                              \
		return Video::bitmap_reg_##access(__VA_ARGS__);                                                               \
	if (addr >= Video::BGOBJ_REG_START && addr < Video::BGOBJ_REG_END) return Video::bgobj_##access(__VA_ARGS__);     \
	if (addr >= Video::DISPLAY_REG_START && addr < Video::DISPLAY_REG_END)                                            \
		return Video::display_##access(__VA_ARGS__);                                                                  \
	if (addr >= Video::IRQ_REG_START && addr < Video::IRQ_REG_END) return Video::irq_##access(__VA_ARGS__);           \
	if (addr >= LoopyIO::BASE_ADDR && addr < LoopyIO::END_ADDR) return LoopyIO::reg_##access(__VA_ARGS__);            \
	if (addr >= Video::DMA_CTRL_START && addr < Video::DMA_CTRL_END) return Video::dma_ctrl_##access(__VA_ARGS__);    \
	if (addr >= Video::DMA_START && addr < Video::DMA_END) return Video::dma_##access(__VA_ARGS__);                   \
	if (addr >= Video::DEBUG_REG_START && addr < Video::DEBUG_REG_END) return Video::debug_##access(__VA_ARGS__);     \
	if (addr >= OCPM::IO_BASE_ADDR && addr < OCPM::IO_END_ADDR) return OCPM::io_##access(__VA_ARGS__);                \
	if (addr >= Sound::CTRL_START && addr < Sound::CTRL_END) return Sound::ctrl_##access(__VA_ARGS__);                \
	if (addr >= Expansion::MAPPED_START && addr < Expansion::MAPPED_END) return Expansion::exp_##access(__VA_ARGS__); \
	return unmapped_##access(__VA_ARGS__);

//Unmapped accesses: the first kUnmappedReportLimit are logged, then only running
//totals at 10x, 100x, ... the limit (a runaway CPU would flood the log)
static const uint32_t kUnmappedReportLimit = 64;
static uint32_t unmapped_count = 0;

void reset_unmapped_reports()
{
	unmapped_count = 0;
}

static bool report_unmapped()
{
	uint32_t n = ++unmapped_count;
	if (n <= kUnmappedReportLimit)
	{
		if (n == kUnmappedReportLimit)
		{
			Log::warn("[SH2] %u unmapped bus accesses; only totals are reported from here", n);
			return false;
		}
		return true;
	}
	uint32_t m = n / kUnmappedReportLimit;
	if (n % kUnmappedReportLimit == 0)
	{
		while (m % 10 == 0)
		{
			m /= 10;
		}
		if (m == 1)
		{
			Log::warn("[SH2] %u unmapped bus accesses so far", n);
		}
	}
	return false;
}

uint8_t unmapped_read8(uint32_t addr)
{
	if (report_unmapped())
		Log::warn("[SH2] unmapped read8 %08X", addr);
	return 0;
}

uint16_t unmapped_read16(uint32_t addr)
{
	if (report_unmapped())
		Log::warn("[SH2] unmapped read16 %08X", addr);
	return 0;
}

uint32_t unmapped_read32(uint32_t addr)
{
	if (report_unmapped())
		Log::warn("[SH2] unmapped read32 %08X", addr);
	return 0;
}

void unmapped_write8(uint32_t addr, uint8_t value)
{
	if (report_unmapped())
		Log::warn("[SH2] unmapped write8 %08X: %02X", addr, value);
}

void unmapped_write16(uint32_t addr, uint16_t value)
{
	if (report_unmapped())
		Log::warn("[SH2] unmapped write16 %08X: %04X", addr, value);
}

void unmapped_write32(uint32_t addr, uint32_t value)
{
	if (report_unmapped())
		Log::warn("[SH2] unmapped write32 %08X: %08X", addr, value);
}

//Data-access bus time, on top of the fetch (read_cycles), as measured on a console:
//cartridge ROM 3 cycles, SRAM 3 per byte, on-chip peripherals 3, the VDP per
//register type (charge_access), BIOS ROM and on-chip RAM 1. Work RAM is 16-bit
//DRAM: 1 cycle per transfer in the open row, 3 for the first on a row change, and
//a same-row write takes one more. The refresh every 244 cycles closes the row;
//accesses to work RAM, and writes to SRAM and the VDP, that meet it wait.
//
//External-bus code (cartridge ROM, work RAM) cannot fetch during an access, so it
//pays it in full. BIOS ROM and on-chip RAM code shares one 32-bit memory port with
//its data accesses; only what exceeds the instruction's own cycle stalls (run()).

//Cycles an access starting now waits for a refresh in progress
static int refresh_wait_at(int64_t now)
{
	int64_t into = now % DRAM_REFRESH_PERIOD;
	return into < DRAM_REFRESH_STATES ? (int)(DRAM_REFRESH_STATES - into) : 0;
}

//Work RAM (512-byte DRAM rows): bus cycles for an access now, updating the open
//row. Work RAM fetches come through here too, so code and data share the row.
static int dram_access(uint32_t addr, int bytes, bool write)
{
	int64_t now = cpu_now();
	int64_t refresh_start = now - now % DRAM_REFRESH_PERIOD;
	int refresh_wait = refresh_wait_at(now);
	int transfers = (bytes == 4) ? 2 : 1;
	uint32_t row = (addr & 0x7FFFF) >> DRAM_ROW_SHIFT;
	bool open = row == sh2.dram_row && sh2.dram_last_access >= refresh_start;
	int64_t start = now + refresh_wait;
	//A same-row write takes a cycle more; in the exception sequence only the PC
	//push behind the SR push does
	bool write_recovery = write && open && (!sh2.in_exception_sequence || sh2.exception_pushed);
	int total = refresh_wait + (open ? transfers + (write_recovery ? 1 : 0) : 2 + transfers);
	sh2.dram_row = row;
	sh2.dram_last_access = start;
	sh2.dram_last_end = now + total;
	sh2.dram_last_was_write = write;
	return total;
}

//Bus cycles of one DMA access starting at `at`: work RAM 3 on a row change, else 1
//to read and 2 to write; tile VRAM writes 5; everything else as the CPU sees it
static int dma_access_base(uint32_t addr, int bytes, bool write, int64_t at)
{
	addr = translate_addr(addr);
	int transfers = (bytes == 4) ? 2 : 1;
	switch (addr >> 24)
	{
	case 0x1:
	{
		int total = 0;
		for (int i = 0; i < transfers; i++)
		{
			int64_t now = at + total;
			int wait = refresh_wait_at(now);
			uint32_t row = (addr & 0x7FFFF) >> DRAM_ROW_SHIFT;
			bool open = row == sh2.dram_row && sh2.dram_last_access >= now - now % DRAM_REFRESH_PERIOD;
			sh2.dram_row = row;
			sh2.dram_last_access = now + wait;
			total += wait + (open ? (write ? 2 : 1) : 3);
		}
		sh2.dram_last_was_write = write;
		sh2.dram_last_end = at + total;
		return total;
	}
	case 0x4:
	{
		uint32_t offset = addr & 0xFFFFFF;
		int each = write ? (offset >= 0x40000 && offset < 0x50000 ? 5 : 3) :
			(offset < 0x40000 ? 5 : offset < 0x50000 ? 7 : 4);
		if (offset < 0x40000 && Video::bitmap_fast_access())
		{
			each--;
		}
		//The VDP's bitmap VRAM hold-up applies to the DMAC too
		if (offset < 0x40000)
		{
			each += Video::bitmap_wait_cycles();
		}
		return each * transfers + (write ? refresh_wait_at(at) : 0);
	}
	case 0x2:
		return 3 * bytes + (write ? refresh_wait_at(at) : 0);
	case 0x5:
	case 0x6:
		return 3 * transfers;
	default:
		return 1;
	}
}

//A refresh due while the DMAC holds the external bus follows the access, so a DMA
//stream pays every refresh in full
int dma_access_cycles(uint32_t addr, int bytes, bool write, int64_t at)
{
	int cycles = dma_access_base(addr, bytes, write, at);
	uint32_t area = translate_addr(addr) >> 24;
	bool external = area == 0x1 || area == 0x2 || area == 0x4 || area == 0x6;
	int64_t next_refresh = at - at % DRAM_REFRESH_PERIOD + DRAM_REFRESH_PERIOD;
	if (external && next_refresh < at + cycles)
	{
		cycles += DRAM_REFRESH_STATES;
	}
	return cycles;
}

int dram_fetch_extra(uint32_t addr)
{
	//read_cycles already charges work RAM fetches their single cycle
	return dram_access(translate_addr(addr), 2, false) - 1;
}

static void charge_access(uint32_t addr, int bytes, bool write)
{
	int transfers;
	int total;

	switch (addr >> 24)
	{
	case 0x1:
		transfers = (bytes == 4) ? 2 : 1;
		total = dram_access(addr, bytes, write);
		if (!write && !sh2.in_exception_sequence)
		{
			sh2.wram_read_this_instr = true;
		}
		break;
	case 0x4:
	{
		//VDP, per transfer: writes 3 cycles; reads 4 for registers, palette and OAM,
		//5 for bitmap VRAM and 7 for tile VRAM
		uint32_t offset = addr & 0xFFFFFF;
		int each = write ? 3 : (offset < 0x40000 ? 5 : offset < 0x50000 ? 7 : 4);
		if (Video::snow_reports)
		{
			Video::note_snow_access(offset, write);
		}
		//BM_MEM_CTRL's fast mode takes a cycle off bitmap VRAM reads and writes
		if (offset < 0x40000 && Video::bitmap_fast_access())
		{
			each--;
		}
		if (offset < 0x40000)
		{
			each += Video::bitmap_wait_cycles();
		}
		transfers = (bytes == 4) ? 2 : 1;
		total = each * transfers + (write ? refresh_wait_at(cpu_now()) : 0);
		break;
	}
	case 0x2:
		//Cartridge SRAM, 8-bit
		transfers = bytes;
		total = 3 * transfers + (write ? refresh_wait_at(cpu_now()) : 0);
		break;
	case 0x5:
	case 0x6:
		//On-chip peripheral registers, cartridge ROM
		transfers = (bytes == 4) ? 2 : 1;
		total = 3 * transfers;
		break;
	case 0xF:
		//On-chip RAM, 32 bits wide: one cycle for any size. From external-bus code the cycle comes after the next instruction's interrupt
		//check, and an interrupt taken there absorbs it.
		transfers = 1;
		total = 1;
		if (sh2.exec_external && !sh2.in_exception_sequence)
		{
			sh2.cycles_left -= sh2.oram_trail;
			sh2.oram_trail = 1;
			total = 0;
		}
		break;
	default:
		return;
	}
	sh2.bus_transfers += transfers;

	if (sh2.exec_external)
	{
		if ((addr >> 24) != 0xF)
		{
			sh2.read_this_instr = true;
		}
		sh2.cycles_left -= total;
	}
	else
	{
		//Internal-bus code: the access queues on the memory port the fetch shares;
		//sh2.cpp run() settles it after the instruction
		bool internal_read = ((addr >> 24) == 0x5 || (addr >> 24) == 0xF) && !write;
		sh2.port_internal_read_only = (sh2.port_access_cycles == 0 || sh2.port_internal_read_only) && internal_read;
		sh2.port_access_cycles += total;
	}
}

//The VDP latches its data bus: an unmapped address or a write-only register reads
//back the last value read from the VDP (not written)
static uint16_t vdp_bus_latch;

static inline bool is_vdp(uint32_t addr)
{
	return (addr >> 24) == 0x4 && !(addr >= Expansion::MAPPED_START && addr < Expansion::MAPPED_END);
}

//Write-only VDP registers that read as the latch: TRIGGER, SYNC_CALIBRATE and the
//debug registers after it
static inline bool is_vdp_write_only(uint32_t addr)
{
	return (addr & 0xFFFFFE) == 0x058006 || (addr >= Video::DEBUG_REG_START && addr < Video::DEBUG_REG_END);
}

static inline bool vdp_mapped(uint32_t addr)
{
	return (addr >= Video::PALETTE_START && addr < Video::PALETTE_END) ||
		(addr >= Video::OAM_START && addr < Video::OAM_END) ||
		(addr >= Video::CAPTURE_START && addr < Video::CAPTURE_END) ||
		(addr >= Video::CTRL_REG_START && addr < Video::CTRL_REG_END) ||
		(addr >= Video::BITMAP_REG_START && addr < Video::BITMAP_REG_END) ||
		(addr >= Video::BGOBJ_REG_START && addr < Video::BGOBJ_REG_END) ||
		(addr >= Video::DISPLAY_REG_START && addr < Video::DISPLAY_REG_END) ||
		(addr >= Video::IRQ_REG_START && addr < Video::IRQ_REG_END) ||
		(addr >= LoopyIO::BASE_ADDR && addr < LoopyIO::END_ADDR) ||
		(addr >= Video::DMA_CTRL_START && addr < Video::DMA_CTRL_END) ||
		(addr >= Video::DMA_START && addr < Video::DMA_END) ||
		(addr >= Sound::CTRL_START && addr < Sound::CTRL_END);
}

static uint16_t vdp_read16(uint32_t addr);

//Area 1 is work RAM on an 8-bit bus (area 9 is the same DRAM at 16 bits): every
//byte read comes off data lines 0-7, the odd byte of its word (measured: a word
//1357 reads as 5757). Writes: area1_write.
static inline bool is_area1(uint32_t addr)
{
	return ((addr >> 24) & 0xF) == 0x1;
}

static uint8_t area1_lane(uint32_t addr)
{
	static bool warned;
	if (!warned)
	{
		warned = true;
		Log::warn("[SH2] work RAM read through area 1 (%08X, 8-bit bus): only the low byte of "
				  "each word comes back. Use 0x09000000.", addr);
	}
	uint8_t* mem = sh2.pagetable[(addr | 1) >> 12];
	return mem ? mem[(addr | 1) & 0xFFF] : 0;
}

//A write through area 1 lands in the low byte of its word whatever the address: a
//word or longword is written a byte at a time, each over the one before (measured)
static void area1_write(uint32_t addr, uint8_t value)
{
	uint8_t* mem = sh2.pagetable[(addr | 1) >> 12];
	if (mem)
	{
		mem[(addr | 1) & 0xFFF] = value;
	}
}

uint8_t read8(uint32_t addr)
{
	if (is_area1(addr))
	{
		addr = translate_addr(addr);
		if (sh2.in_execute) charge_access(addr, 1, false);
		return area1_lane(addr);
	}
	//Area 4 is the VDP on an 8-bit bus: either byte reads the low byte of its word
	if (((addr >> 24) & 0xF) == 0x4)
	{
		uint32_t t = translate_addr(addr);
		if (is_vdp(t) && !sh2.pagetable[t >> 12])
		{
			if (sh2.in_execute) charge_access(t, 1, false);
			if (!is_repeatable_read(t)) sh2.idle_unsafe_read = true;
			return (uint8_t)vdp_read16(t & ~1u);
		}
	}
	addr = translate_addr(addr);
	if (sh2.in_execute) charge_access(addr, 1, false);
	uint8_t* mem = sh2.pagetable[addr >> 12];
	if (mem)
	{
		return mem[addr & 0xFFF];
	}

	if (!is_repeatable_read(addr)) sh2.idle_unsafe_read = true;
	ReadBias bias(addr);
	MMIO_ACCESS(read8, addr);
}

//A misaligned word or longword access is an address error. The access still
//happens at the aligned address (measured); sh2.cpp takes the exception after the
//instruction. The exception sequence's own accesses never raise one.
static inline uint32_t check_alignment(uint32_t addr, uint32_t mask)
{
	if ((addr & mask) && sh2.in_execute && !sh2.in_exception_sequence)
	{
		sh2.data_address_error = true;
	}
	return addr & ~mask;
}

static uint16_t mmio_read16(uint32_t addr)
{
	ReadBias bias(addr);
	MMIO_ACCESS(read16, addr);
}

static uint16_t vdp_read16(uint32_t addr)
{
	if (is_vdp_write_only(addr) || !vdp_mapped(addr))
	{
		return vdp_bus_latch;
	}
	vdp_bus_latch = mmio_read16(addr);
	return vdp_bus_latch;
}

uint16_t read16(uint32_t addr)
{
	if (is_area1(addr))
	{
		addr = check_alignment(translate_addr(addr), 1u);
		if (sh2.in_execute) charge_access(addr, 2, false);
		uint8_t b = area1_lane(addr);
		return (uint16_t)(b << 8 | b);
	}
	addr = check_alignment(translate_addr(addr), 1u);
	if (sh2.in_execute) charge_access(addr, 2, false);
	uint8_t* mem = sh2.pagetable[addr >> 12];
	if (mem)
	{
		uint16_t value;
		memcpy(&value, mem + (addr & 0xFFF), 2);
		value = Common::bswp16(value);
		if ((addr >> 24) == 0x4)
		{
			vdp_bus_latch = value;
		}
		return value;
	}

	if (!is_repeatable_read(addr)) sh2.idle_unsafe_read = true;
	if (is_vdp(addr))
	{
		return vdp_read16(addr);
	}
	return mmio_read16(addr);
}

uint32_t read32(uint32_t addr)
{
	if (is_area1(addr))
	{
		addr = check_alignment(translate_addr(addr), 3u);
		if (sh2.in_execute) charge_access(addr, 4, false);
		uint32_t hi = area1_lane(addr), lo = area1_lane(addr + 2);
		return hi << 24 | hi << 16 | lo << 8 | lo;
	}
	addr = check_alignment(translate_addr(addr), 3u);
	if (sh2.in_execute) charge_access(addr, 4, false);
	uint8_t* mem = sh2.pagetable[addr >> 12];
	if (mem)
	{
		uint32_t value;
		memcpy(&value, mem + (addr & 0xFFF), 4);
		value = Common::bswp32(value);
		if ((addr >> 24) == 0x4)
		{
			vdp_bus_latch = (uint16_t)value;
		}
		return value;
	}

	if (!is_repeatable_read(addr)) sh2.idle_unsafe_read = true;
	if (is_vdp(addr))
	{
		//Two 16-bit transfers on the VDP's bus
		uint32_t hi = vdp_read16(addr);
		return (hi << 16) | vdp_read16(addr + 2);
	}
	ReadBias bias(addr);
	MMIO_ACCESS(read32, addr);
}

//A byte write to tile VRAM, palette or OAM stores the whole 16-bit bus: the undriven
//half still holds the last word the CPU fetched, the instruction at PC (two after
//the write). Measured: the other byte never depends on the value written or the
//word that was there.
static bool vdp_word_memory(uint32_t addr)
{
	return (addr & 0x0FFF0000) == 0x04040000 || (addr >= (uint32_t)Video::OAM_START && addr < (uint32_t)Video::OAM_END) ||
		   (addr >= (uint32_t)Video::PALETTE_START && addr < (uint32_t)Video::PALETTE_END);
}

static void vdp_byte_write(uint32_t addr, uint8_t value)
{
	static bool warned;
	if (!warned)
	{
		warned = true;
		Log::warn("[Video] byte write to tile VRAM, palette or OAM (%08X): on a console the other byte "
				  "of the word is overwritten. Write words.", addr);
	}
	uint32_t pc = translate_addr(sh2.pc);
	uint8_t* code = sh2.pagetable[pc >> 12];
	uint16_t bus = code ? (uint16_t)(code[pc & 0xFFF] << 8 | code[(pc + 1) & 0xFFF]) : 0xFFFF;
	uint16_t word = (addr & 1) ? (uint16_t)((bus & 0xFF00) | value) : (uint16_t)(value << 8 | (bus & 0xFF));
	addr &= ~1u;
	uint8_t* mem = sh2.pagetable[addr >> 12];
	if (mem)
	{
		mem[addr & 0xFFF] = (uint8_t)(word >> 8);
		mem[(addr + 1) & 0xFFF] = (uint8_t)word;
		return;
	}
	MMIO_ACCESS(write16, addr, word);
}

void write8(uint32_t addr, uint8_t value)
{
	sh2.idle_wrote_mem = true;
	bool area1 = is_area1(addr);
	addr = translate_addr(addr);
	if (sh2.in_execute) charge_access(addr, 1, true);
	uint8_t* mem = sh2.pagetable[addr >> 12];
	if (sh2.in_execute && vdp_word_memory(addr))
	{
		vdp_byte_write(addr, value);
		return;
	}
	if (area1)
	{
		area1_write(addr, value);
		return;
	}
	if (mem)
	{
		mem[addr & 0xFFF] = value;
		return;
	}

	MMIO_ACCESS(write8, addr, value);
}

void write16(uint32_t addr, uint16_t value)
{
	sh2.idle_wrote_mem = true;
	bool area1 = is_area1(addr);
	addr = check_alignment(translate_addr(addr), 1u);
	if (sh2.in_execute) charge_access(addr, 2, true);
	if (area1)
	{
		area1_write(addr, (uint8_t)value);
		return;
	}
	uint8_t* mem = sh2.pagetable[addr >> 12];
	if (mem)
	{
		value = Common::bswp16(value);
		memcpy(mem + (addr & 0xFFF), &value, 2);
		return;
	}
	MMIO_ACCESS(write16, addr, value);
}

void write32(uint32_t addr, uint32_t value)
{
	sh2.idle_wrote_mem = true;
	bool area1 = is_area1(addr);
	addr = check_alignment(translate_addr(addr), 3u);
	if (sh2.in_execute) charge_access(addr, 4, true);
	if (area1)
	{
		area1_write(addr, (uint8_t)(value >> 16));
		area1_write(addr + 2, (uint8_t)value);
		return;
	}
	uint8_t* mem = sh2.pagetable[addr >> 12];
	if (mem)
	{
		value = Common::bswp32(value);
		memcpy(mem + (addr & 0xFFF), &value, 4);
		return;
	}
	MMIO_ACCESS(write32, addr, value);
}

uint8_t* page_ptr(uint32_t addr)
{
	addr = translate_addr(addr);
	return sh2.pagetable[addr >> 12];
}

int read_cycles(uint32_t addr)
{
	//TODO: some depend on wait-state config, DRAM refresh etc. Check appropriately.
	//Maybe also use each module's mapped address instead of hardcoded areas, for now it's too hard.

	int base_cycles = 1;
	int wait_cycles = 0;

	addr = translate_addr(addr);

	switch (addr >> 24)
	{
		case 0x0: //BIOS
			base_cycles = 1;
			break;
		case 0x1: //DRAM
			base_cycles = 1; //row changes and refresh: dram_fetch_extra
			break;
		case 0x2: //CARTRAM
			base_cycles = 3;
			break;
		case 0x4: //VDP & MMIO
			base_cycles = 2;
			wait_cycles = 1;
			if ((addr & 0x3FFFFF) >= 0x58000)
			{
				wait_cycles = 2;
			}
			break;
		case 0x5: //SH peripherals
			base_cycles = 3;
			break;
		case 0x6: //CARTROM
			base_cycles = 3;
			break;
		case 0xF: //ORAM (unmirrored)
			base_cycles = 1;
			break;
		default:
			break;
	}

	return base_cycles + wait_cycles;
}

}  // namespace SH2::Bus