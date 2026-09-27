#pragma once
#include <cstdint>

#include "core/savestate.h"

namespace SH2::OCPM::WDT
{

void initialize();

//addr is the offset from 0x5FFFFB8: 0 TCSR, 1 TCNT, 3 RSTCSR (reads, bytes);
//0 and 2 for the keyed word writes
uint8_t read8(uint32_t addr);
uint16_t read16(uint32_t addr);
void write16(uint32_t addr, uint16_t value);

void save_state(SaveState::Snapshot& ss);
void load_state(SaveState::Snapshot& ss);

}
