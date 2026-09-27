#include <cassert>
#include <log/log.h>
#include "core/sh2/peripherals/sh2_dmac.h"
#include "core/sh2/peripherals/sh2_intc.h"
#include "core/sh2/sh2_local.h"

namespace SH2::OCPM::INTC
{

struct State
{
	uint32_t vectors[(int)IRQ::NumIrq];
	int prios[(int)IRQ::NumIrq];

	int pending_irqs[(int)IRQ::NumIrq];
	int irq_offs[(int)IRQ::NumIrq];

	//Latched edge requests, held until the CPU accepts them
	int edge_pending[(int)IRQ::NumIrq];

	uint16_t icr;
	//IPRE's low nibble: reserved, but it reads back as written
	uint16_t ipre_low;
	bool irq1_pin_enabled;  //PACR1 selects the IRQ1 function for PA13
	bool irq0_pin_enabled;  //and IRQ0 for PA12
	bool irq2_pin_enabled;  //and IRQ2 for PA14
	bool dreq0_pin_enabled; //PA13 as DREQ0 instead
	bool irq1_line;         //PA13 driven low by the VDP
	bool irq1_low;          //the level the INTC last saw: pin routed and low
	int presented;          //the source last offered to the CPU, -1 if none
};

static State state;

//ICR bit 6 (IRQ1S): 1 = falling edge, 0 = low level
constexpr uint16_t ICR_IRQ1S = 0x0040;

static void send_irq_signal()
{
	int vector = 0;
	int highest_prio = 0;
	int highest_id = -1;

	for (int id = 0; id < (int)IRQ::NumIrq; id++)
	{
		if (state.pending_irqs[id] || state.edge_pending[id])
		{
			if (state.prios[id] > highest_prio)
			{
				highest_prio = state.prios[id];
				vector = state.vectors[id] + state.irq_offs[id];
				highest_id = id;
			}
		}
	}

	state.presented = highest_id;
	SH2::assert_irq(vector, highest_prio);
}

//Re-derives IRQ1 from the pin. Only a fall while the pin is routed to the INTC is
//an edge, not switching the sense bit or pin function.
static void update_irq1(bool allow_edge)
{
	constexpr int id = (int)IRQ::IRQ1;
	bool low = state.irq1_pin_enabled && state.irq1_line;
	state.irq_offs[id] = 0;

	if (state.icr & ICR_IRQ1S)
	{
		state.pending_irqs[id] = false;
		if (allow_edge && low && !state.irq1_low)
		{
			state.edge_pending[id] = true;
		}
	}
	else
	{
		state.pending_irqs[id] = low;
	}
	state.irq1_low = low;
	send_irq_signal();
}

void initialize()
{
	state = {};
	state.presented = -1;

	//NMI and UserBreak have fixed priorities, everything else is configurable
	state.prios[(int)IRQ::NMI] = 16;
	state.prios[(int)IRQ::UserBreak] = 15;

	state.vectors[(int)IRQ::NMI] = 11;
	state.vectors[(int)IRQ::UserBreak] = 12;

	for (int i = 0; i < 8; i++)
	{
		int id = (int)IRQ::IRQ0;
		state.vectors[id + i] = 64 + i;
	}

	for (int i = 0; i < 4; i++)
	{
		int id = (int)IRQ::DMAC0;
		//DEI0-3 at 72, 74, 76, 78 (SH7021 manual; 80 is ITU0's IMIA)
		state.vectors[id + i] = 72 + (i * 2);
	}

	for (int i = 0; i < 5; i++)
	{
		int id = (int)IRQ::ITU0;
		state.vectors[id + i] = 80 + (i * 4);
	}

	for (int i = 0; i < 2; i++)
	{
		int id = (int)IRQ::SCI0;
		state.vectors[id + i] = 100 + (i * 4);
	}

	//WDT ITI and the refresh controller's CMI (SH7021 manual)
	state.vectors[(int)IRQ::WDT] = 112;
	state.vectors[(int)IRQ::REF] = 113;

	//TODO: remaining interrupts
}

uint16_t read16(uint32_t addr)
{
	addr &= 0xF;

	switch (addr)
	{
	case 0x04:
	{
		// IPRA
		uint16_t result = state.prios[(int)IRQ::IRQ0] << 12;
		result |= state.prios[(int)IRQ::IRQ1] << 8;
		result |= state.prios[(int)IRQ::IRQ2] << 4;
		result |= state.prios[(int)IRQ::IRQ3];
		return result;
	}
	case 0x06:
	{
		// IPRB
		uint16_t result = state.prios[(int)IRQ::IRQ4] << 12;
		result |= state.prios[(int)IRQ::IRQ5] << 8;
		result |= state.prios[(int)IRQ::IRQ6] << 4;
		result |= state.prios[(int)IRQ::IRQ7];
		return result;
	}
	case 0x08:
	{
		// IPRC
		uint16_t result =  state.prios[(int)IRQ::DMAC0] << 12;
		result |= state.prios[(int)IRQ::DMAC2] << 8;
		result |= state.prios[(int)IRQ::ITU0] << 4;
		result |= state.prios[(int)IRQ::ITU1];
		return result;
	}
	case 0x0A:
	{
		// IPRD
		uint16_t result = state.prios[(int)IRQ::ITU2] << 12;
		result |= state.prios[(int)IRQ::ITU3] << 8;
		result |= state.prios[(int)IRQ::ITU4] << 4;
		result |= state.prios[(int)IRQ::SCI0];
		return result;
	}
	case 0x0C:
	{
		// IPRE
		uint16_t result = state.prios[(int)IRQ::SCI1] << 12;
		result |= state.prios[(int)IRQ::PRT] << 8;
		result |= state.prios[(int)IRQ::WDT] << 4;
		return result | state.ipre_low;
	}
	case 0x0E:
		// ICR. Bit 15 (NMIL), the NMI pin's level, reads high
		return state.icr | 0x8000;
	default:
		LOG_UNEMULATED("[INTC] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
		return 0;
	}
}

uint8_t read8(uint32_t addr)
{
	uint8_t result;
	if ((addr & 1) == 0)
	{
		// Read the first (high) byte
		result = read16(addr) >> 8;
	}
	else
	{
		// Read the second (low) byte
		result = read16(addr - 1) & 0xFF;
	}
	return result;
}

void write16(uint32_t addr, uint16_t value)
{
	addr &= 0xF;

	switch (addr)
	{
	case 0x04:
		// IPRA
		state.prios[(int)IRQ::IRQ0] = value >> 12;
		state.prios[(int)IRQ::IRQ1] = (value >> 8) & 0x0F;
		state.prios[(int)IRQ::IRQ2] = (value >> 4) & 0x0F;
		state.prios[(int)IRQ::IRQ3] = value & 0x0F;
		break;
	case 0x06:
		// IPRB
		state.prios[(int)IRQ::IRQ4] = value >> 12;
		state.prios[(int)IRQ::IRQ5] = (value >> 8) & 0x0F;
		state.prios[(int)IRQ::IRQ6] = (value >> 4) & 0x0F;
		state.prios[(int)IRQ::IRQ7] = value & 0x0F;
		break;
	case 0x08:
		// IPRC
		state.prios[(int)IRQ::DMAC0] = state.prios[(int)IRQ::DMAC1] = value >> 12;
		state.prios[(int)IRQ::DMAC2] = state.prios[(int)IRQ::DMAC3] = (value >> 8) & 0x0F;
		state.prios[(int)IRQ::ITU0] = (value >> 4) & 0x0F;
		state.prios[(int)IRQ::ITU1] = value & 0x0F;
		break;
	case 0x0A:
		// IPRD
		state.prios[(int)IRQ::ITU2] = value >> 12;
		state.prios[(int)IRQ::ITU3] = (value >> 8) & 0x0F;
		state.prios[(int)IRQ::ITU4] = (value >> 4) & 0x0F;
		state.prios[(int)IRQ::SCI0] = value & 0x0F;
		break;
	case 0x0C:
		// IPRE
		state.prios[(int)IRQ::SCI1] = value >> 12;
		state.prios[(int)IRQ::PRT] = (value >> 8) & 0x0F;
		state.prios[(int)IRQ::WDT] = state.prios[(int)IRQ::REF] = (value >> 4) & 0x0F;
		state.ipre_low = value & 0x0F;
		break;
	case 0x0E:
	{
		// ICR: NMIE (bit 8) and the IRQ0-7 sense bits (bits 7-0)
		uint16_t old = state.icr;
		state.icr = value & 0x01FF;
		if ((old ^ state.icr) & ICR_IRQ1S)
		{
			update_irq1(false);
		}
		return;
	}
	default:
		LOG_UNEMULATED("[INTC] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
		return;
	}

	//A priority change can change which request wins, or unmask one
	send_irq_signal();
}

void write8(uint32_t addr, uint8_t value)
{
	uint16_t tmp;
	if ((addr & 1) == 0)
	{
		// Write the first (high) byte by masking
		tmp = read16(addr) & 0x00FF;
		tmp |= value << 8;
		write16(addr, tmp);
	}
	else
	{
		// Write the second (low) byte by masking
		tmp = read16(addr - 1) & 0xFF00;
		tmp |= value;
		write16(addr - 1, tmp);
	}
}

void save_state(SaveState::Snapshot& ss)
{
	ss.begin_section(SaveState::fourcc("INTC"));
	ss.write(state.vectors);
	ss.write(state.prios);
	ss.write(state.pending_irqs);
	ss.write(state.irq_offs);
	ss.write(state.edge_pending);
	ss.write(state.icr);
	ss.write(state.ipre_low);
	ss.write(state.irq1_pin_enabled);
	ss.write(state.irq0_pin_enabled);
	ss.write(state.irq2_pin_enabled);
	ss.write(state.dreq0_pin_enabled);
	ss.write(state.irq1_line);
	ss.write(state.irq1_low);
	ss.write(state.presented);
}

void load_state(SaveState::Snapshot& ss)
{
	ss.expect_section(SaveState::fourcc("INTC"));
	ss.read(state.vectors);
	ss.read(state.prios);
	ss.read(state.pending_irqs);
	ss.read(state.irq_offs);
	ss.read(state.edge_pending);
	ss.read(state.icr);
	ss.read(state.ipre_low);
	ss.read(state.irq1_pin_enabled);
	ss.read(state.irq0_pin_enabled);
	ss.read(state.irq2_pin_enabled);
	ss.read(state.dreq0_pin_enabled);
	ss.read(state.irq1_line);
	ss.read(state.irq1_low);
	ss.read(state.presented);
}

void assert_irq(IRQ irq, int vector_offs)
{
	state.pending_irqs[(int)irq] = true;
	state.irq_offs[(int)irq] = vector_offs;
	send_irq_signal();
}

void deassert_irq(IRQ irq)
{
	state.pending_irqs[(int)irq] = false;
	send_irq_signal();
}

void pulse_irq(IRQ irq, int vector_offs)
{
	if ((irq == IRQ::IRQ0 && !state.irq0_pin_enabled) || (irq == IRQ::IRQ2 && !state.irq2_pin_enabled))
	{
		return;
	}
	state.edge_pending[(int)irq] = true;
	state.irq_offs[(int)irq] = vector_offs;
	send_irq_signal();
}

void set_irq1_pin_enabled(bool enabled)
{
	state.irq1_pin_enabled = enabled;
	update_irq1(true);
}

void set_irq0_pin_enabled(bool enabled)
{
	state.irq0_pin_enabled = enabled;
}

void set_irq2_pin_enabled(bool enabled)
{
	state.irq2_pin_enabled = enabled;
}

void set_irq1_line(bool asserted)
{
	bool fell = asserted && !state.irq1_line;
	state.irq1_line = asserted;
	update_irq1(true);
	if (state.dreq0_pin_enabled)
	{
		DMAC::dreq0(asserted, fell);
	}
}

bool irq1_line_low()
{
	return state.irq1_line;
}

void set_dreq0_pin_enabled(bool enabled)
{
	state.dreq0_pin_enabled = enabled;
}

void acknowledge()
{
	//Taking an edge request consumes it. A level request stays; SR.IMASK holds it
	//off until the handler returns
	if (state.presented >= 0)
	{
		state.edge_pending[state.presented] = false;
	}
	send_irq_signal();
}

}