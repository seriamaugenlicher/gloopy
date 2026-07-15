#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include "core/cart.h"
#include "core/memory.h"

namespace Cart
{

struct State
{
	std::vector<uint8_t> rom;
	std::vector<uint8_t> sram;
	std::string sram_file_path;
};

static State state;

void initialize(Config::CartInfo& info)
{
	state = {};

	state.rom = info.rom;
	state.sram = info.sram;
	state.sram_file_path = info.sram_file_path;

	//Ensure that the ROM and SRAM are aligned to a 4 KB boundary
	if (state.rom.size() & 0xFFF)
	{
		size_t new_size = (state.rom.size() + 0xFFF) & ~0xFFF;
		state.rom.resize(new_size, 0xFF);
	}

	if (state.sram.size() & 0xFFF)
	{
		size_t new_size = (state.sram.size() + 0xFFF) & ~0xFFF;
		state.sram.resize(new_size, 0xFF);
	}

	Memory::map_sh2_pagetable(state.rom.data(), ROM_START, state.rom.size());
	Memory::map_sh2_pagetable(state.sram.data(), SRAM_START, state.sram.size());
}

void shutdown(Config::CartInfo& info)
{
	info.sram = state.sram;
}

uint8_t* get_sram_ptr()
{
	return state.sram.data();
}

size_t get_sram_size()
{
	return state.sram.size();
}

void save_state(SaveState::Snapshot& ss)
{
	ss.begin_section(SaveState::fourcc("CART"));

	//ROM identity: size plus the checksum field from the ROM header (offset 0x8),
	//so a state can't be loaded on top of a different game
	uint64_t rom_size = state.rom.size();
	uint32_t rom_checksum = 0;
	if (state.rom.size() >= 12)
	{
		memcpy(&rom_checksum, state.rom.data() + 8, 4);
	}
	ss.write(rom_size);
	ss.write(rom_checksum);

	uint64_t sram_size = state.sram.size();
	ss.write(sram_size);
	ss.write_blob(state.sram.data(), state.sram.size());
}

void load_state(SaveState::Snapshot& ss)
{
	ss.expect_section(SaveState::fourcc("CART"));

	uint64_t rom_size;
	uint32_t rom_checksum;
	ss.read(rom_size);
	ss.read(rom_checksum);

	uint32_t cur_checksum = 0;
	if (state.rom.size() >= 12)
	{
		memcpy(&cur_checksum, state.rom.data() + 8, 4);
	}

	if (rom_size != state.rom.size() || rom_checksum != cur_checksum)
	{
		throw std::runtime_error("save state belongs to a different ROM");
	}

	uint64_t sram_size;
	ss.read(sram_size);
	if (sram_size != state.sram.size())
	{
		throw std::runtime_error("save state SRAM size mismatch");
	}
	ss.read_blob(state.sram.data(), state.sram.size());
}

}