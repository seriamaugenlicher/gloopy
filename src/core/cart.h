#pragma once
#include "core/config.h"
#include "core/savestate.h"

namespace Cart
{

constexpr static int SRAM_START = 0x02000000;
constexpr static int ROM_START = 0x06000000;

void initialize(Config::CartInfo& info);
void shutdown(Config::CartInfo& info);

//Direct SRAM access for the libretro frontend (RETRO_MEMORY_SAVE_RAM). The
//buffer is stable for the lifetime of the loaded cart, and the frontend writes
//it out to a .srm itself; libretro has no way for a core to request a flush, so
//when that happens is the frontend's policy. The standalone emulator instead
//wrote its own .sav from here every 60 frames.
uint8_t* get_sram_ptr();
size_t get_sram_size();

void save_state(SaveState::Snapshot& ss);
void load_state(SaveState::Snapshot& ss);

}
