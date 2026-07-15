#pragma once
#include <cstdint>

namespace SH2::Bus
{

uint8_t read8(uint32_t addr);
uint16_t read16(uint32_t addr);
uint32_t read32(uint32_t addr);

void write8(uint32_t addr, uint8_t value);
void write16(uint32_t addr, uint16_t value);
void write32(uint32_t addr, uint32_t value);

int read_cycles(uint32_t addr);
int write_cycles(uint32_t addr);

//Direct backing-memory pointer for the 4KB page containing addr, or null
//for MMIO. Used by the CPU's instruction-fetch fast path; the memory map is
//static after initialization so the returned pointer stays valid.
uint8_t* page_ptr(uint32_t addr);

}