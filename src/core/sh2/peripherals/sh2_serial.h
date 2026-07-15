#pragma once
#include <cstdint>
#include <functional>

#include "core/savestate.h"

namespace SH2::OCPM::Serial
{

void initialize();

uint8_t read8(uint32_t addr);

void write8(uint32_t addr, uint8_t value);

void set_tx_callback(int port, std::function<void(uint8_t)> callback);

void save_state(SaveState::Snapshot& ss);
void load_state(SaveState::Snapshot& ss);

}
