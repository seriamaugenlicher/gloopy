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

bool motor_move_hook(uint32_t addr);
bool printer_hook(uint32_t addr);

}  // namespace Printer