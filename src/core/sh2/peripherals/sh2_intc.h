#pragma once
#include <cstdint>

#include "core/savestate.h"

namespace SH2::OCPM::INTC
{

enum class IRQ
{
	NMI,
	UserBreak,

	IRQ0,
	IRQ1,
	IRQ2,
	IRQ3,
	IRQ4,
	IRQ5,
	IRQ6,
	IRQ7,

	DMAC0,
	DMAC1,
	DMAC2,
	DMAC3,

	ITU0,
	ITU1,
	ITU2,
	ITU3,
	ITU4,

	SCI0,
	SCI1,

	PRT,

	WDT,

	REF,

	NumIrq
};

void initialize();

uint8_t read8(uint32_t addr);
void write8(uint32_t addr, uint8_t value);
uint16_t read16(uint32_t addr);
void write16(uint32_t addr, uint16_t value);

//Level sources (on-chip peripherals): the request stands while asserted
void assert_irq(IRQ irq, int vector_offs);
void deassert_irq(IRQ irq);

//Edge sources (the VDP's NMI and IRQ0 pulses): latched until the CPU accepts it
void pulse_irq(IRQ irq, int vector_offs);

//The IRQ1 pin (PA13), driven by the VDP's raster DMA signal. ICR.IRQ1S picks
//falling-edge or low-level sense
void set_irq1_pin_enabled(bool enabled);
void set_irq0_pin_enabled(bool enabled);
void set_irq2_pin_enabled(bool enabled);
void set_dreq0_pin_enabled(bool enabled);
void set_irq1_line(bool asserted);
//The raster DMA signal is low (PA13 reads 0)
bool irq1_line_low();

//The CPU accepted the request it was presented; consumes an edge latch
void acknowledge();

void save_state(SaveState::Snapshot& ss);
void load_state(SaveState::Snapshot& ss);

}