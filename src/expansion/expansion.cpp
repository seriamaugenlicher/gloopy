#include "expansion.h"

#include <memory>
#include <vector>

#include "expansion/msm665x/msm665x.h"
#include "log/log.h"

namespace Expansion
{

void initialize(Config::CartInfo& cart)
{
	if (!cart.is_loaded()) return;

	//The expansion chip is offered to every cart; see msm665x.cpp for why
	MSM665X::enable();
	MSM665X::initialize(cart.rom_path);
}

void shutdown()
{
	if (MSM665X::is_enabled()) MSM665X::shutdown();
}

uint8_t exp_read8(uint32_t addr)
{
	Log::warn("[Expansion] unmapped read8 %08X", addr);

	return 0;
}
uint16_t exp_read16(uint32_t addr)
{
	Log::warn("[Expansion] unmapped read16 %08X", addr);

	return 0;
}
uint32_t exp_read32(uint32_t addr)
{
	Log::warn("[Expansion] unmapped read32 %08X", addr);

	return 0;
}
void exp_write8(uint32_t addr, uint8_t value)
{
	if (MSM665X::is_enabled())
	{
		MSM665X::write8(addr, value);
	}
	else
	{
		Log::warn("[Expansion] unmapped write8 %08X: %02X", addr, value);
	}
}
void exp_write16(uint32_t addr, uint16_t value)
{
	Log::warn("[Expansion] unmapped write16 %08X: %04X", addr, value);
}
void exp_write32(uint32_t addr, uint32_t value)
{
	Log::warn("[Expansion] unmapped write32 %08X: %08X", addr, value);
}

void save_state(SaveState::Snapshot& ss)
{
	MSM665X::save_state(ss);
}

void load_state(SaveState::Snapshot& ss)
{
	MSM665X::load_state(ss);
}

}  // namespace Expansion
