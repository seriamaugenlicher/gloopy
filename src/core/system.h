#pragma once
#include <string>

#include "core/config.h"
#include "core/savestate.h"

namespace System
{

void initialize(Config::SystemInfo& config);
void shutdown(Config::SystemInfo& config);

void run();

uint16_t* get_display_output();

bool save_state(const std::string& path);
bool load_state(const std::string& path);

//In-memory savestates for the libretro frontend
void save_state(SaveState::Snapshot& ss);
bool load_state(SaveState::Snapshot& ss);

}