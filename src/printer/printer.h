#pragma once

#include <core/config.h>

#include <cstdint>

namespace Printer
{

void initialize(Config::SystemInfo& config);
void shutdown();

//Applied live: the format only decides how the next print is encoded, so there is
//no reason to make the player reload the game to change it
void set_image_type(int image_type);

//Also live: the directory is only consulted when the game prints, and an empty
//one switches the printer off (the BIOS then reports no seal cartridge)
void set_output_directory(const fs::path& dir);

}  // namespace Printer