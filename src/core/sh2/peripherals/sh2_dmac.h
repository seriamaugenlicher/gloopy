#pragma once
#include <cstdint>

#include "core/savestate.h"

namespace SH2::OCPM::DMAC
{

enum class DREQ
{
	External,
	Reserved,
	External2,
	External3,

	RXI0,
	TXI0,
	RXI1,
	TXI1,

	IMIA0,
	IMIA1,
	IMIA2,
	IMIA3,

	Auto,
	Reserved2,
	Reserved3,
	Reserved4,

	NumDreq
};

void initialize();

void send_dreq(DREQ dreq);
void dreq0(bool low, bool fell);
void clear_dreq(DREQ dreq);

uint16_t read16(uint32_t addr);

void write16(uint32_t addr, uint16_t value);
void write32(uint32_t addr, uint32_t value);

bool is_dma_access();

//The CPU loop's side of a running transfer (see sh2_dmac.cpp)
bool run(bool cpu_idle);
int64_t next_start();
void cpu_accessed(int transfers);

void save_state(SaveState::Snapshot& ss);
void load_state(SaveState::Snapshot& ss);

}