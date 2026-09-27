#pragma once
#include "core/config.h"
#include "core/savestate.h"

namespace System
{

void initialize(Config::SystemInfo& config);
void shutdown(Config::SystemInfo& config);

void run();

uint16_t* get_display_output();

//In-memory savestates for the libretro frontend
void save_state(SaveState::Snapshot& ss);
bool load_state(SaveState::Snapshot& ss);

}