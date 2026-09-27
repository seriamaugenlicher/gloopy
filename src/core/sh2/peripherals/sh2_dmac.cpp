#include <log/log.h>

#include <cassert>
#include "core/sh2/peripherals/sh2_dmac.h"
#include "core/sh2/peripherals/sh2_intc.h"
#include "core/sh2/sh2_local.h"
#include "core/sh2/sh2_bus.h"
#include "core/timing.h"

#include <cstdint>

namespace SH2::OCPM::DMAC
{

struct Channel
{
	uint32_t src_addr;
	uint32_t dst_addr;
	uint32_t transfer_size;

	struct Ctrl
	{
		int enable;
		int finished;
		//TE was read as 1 since it was set: only then does writing 0 clear it
		int finished_read;
		int irq_enable;
		int transfer_16bit;
		int is_burst;
		int unk_ack_bits;
		int mode;
		int src_step;
		int dst_step;
	};

	Ctrl ctrl;

	//A transfer in progress: requested at start_time minus the start-up, moving
	//once started. Edge-sensed requests are counted, one unit each.
	bool running;
	bool started;
	int64_t start_time;
	uint32_t edge_units;

	uint16_t get_ctrl()
	{
		uint16_t result = ctrl.enable;
		result |= ctrl.finished << 1;
		result |= ctrl.irq_enable << 2;
		result |= ctrl.transfer_16bit << 3;
		result |= ctrl.is_burst << 4;
		result |= ctrl.unk_ack_bits << 5;
		result |= ctrl.mode << 8;
		result |= ctrl.src_step << 12;
		result |= ctrl.dst_step << 14;
		return result;
	}

	//DEI: requested while the transfer-end flag and CHCR IE are both set, so it
	//stands until the program clears either (SH7021 manual)
	void update_irq(int id)
	{
		auto irq = (INTC::IRQ)((int)INTC::IRQ::DMAC0 + id);
		if (ctrl.finished && ctrl.irq_enable)
		{
			INTC::assert_irq(irq, 0);
		}
		else
		{
			INTC::deassert_irq(irq);
		}
	}

	void set_ctrl(uint16_t value)
	{
		ctrl.enable = value & 0x1;
		if (ctrl.finished_read && !(value & 0x2))
		{
			ctrl.finished = 0;
			ctrl.finished_read = 0;
		}
		ctrl.irq_enable = (value >> 2) & 0x1;
		ctrl.transfer_16bit = (value >> 3) & 0x1;
		ctrl.is_burst = (value >> 4) & 0x1;
		ctrl.unk_ack_bits = (value >> 5) & 0x7;
		ctrl.mode = ((value >> 8) & 0xF);
		ctrl.src_step = (value >> 12) & 0x3;
		ctrl.dst_step = (value >> 14) & 0x3;
	}

	bool edge_sensed()
	{
		return ctrl.mode == (int)DREQ::External && (ctrl.unk_ack_bits & 1);
	}

	void activate();
	bool can_move();
	int unit();
};

struct State
{
	Channel chan[4];
	int dreqs[(int)DREQ::NumDreq];
	uint16_t ctrl;
	//Cycle stealing: units the DMAC takes before the CPU's next instruction
	int owed;
};

static State state;
static bool in_dma_state;

//Cycles from the CHCR write (or the request) to the DMAC's first read
constexpr int64_t DMA_START_UP = 5;

void Channel::activate()
{
	if (!running)
	{
		running = true;
		started = false;
		start_time = Timing::get_timestamp(Timing::CPU_TIMER) + DMA_START_UP;
		sh2.dma_busy = true;
	}
}

bool Channel::can_move()
{
	if (!(state.ctrl & 0x1) || !ctrl.enable || ctrl.finished || !transfer_size)
	{
		return false;
	}
	return edge_sensed() ? edge_units > 0 : state.dreqs[ctrl.mode] != 0;
}

//One unit, at the current time: its bus cycles
int Channel::unit()
{
	int bytes = ctrl.transfer_16bit ? 2 : 1;
	int src_step = ctrl.src_step == 1 ? 1 : ctrl.src_step == 2 ? -1 : 0;
	int dst_step = ctrl.dst_step == 1 ? 1 : ctrl.dst_step == 2 ? -1 : 0;

	in_dma_state = true;
	if (ctrl.transfer_16bit)
	{
		SH2::Bus::write16(dst_addr, SH2::Bus::read16(src_addr));
	}
	else
	{
		SH2::Bus::write8(dst_addr, SH2::Bus::read8(src_addr));
	}
	in_dma_state = false;
	int64_t now = cpu_now();
	int cycles = SH2::Bus::dma_access_cycles(src_addr, bytes, false, now);
	cycles += SH2::Bus::dma_access_cycles(dst_addr, bytes, true, now + cycles);

	src_addr += src_step * bytes;
	dst_addr += dst_step * bytes;
	transfer_size--;
	if (edge_units)
	{
		edge_units--;
	}
	if (!transfer_size)
	{
		ctrl.finished = true;
	}
	return cycles;
}

//Called by the CPU loop while a transfer runs. Burst mode holds the bus until the
//transfer ends; cycle steal takes one unit per CPU bus access. Returns true if it
//took time.
bool run(bool cpu_idle)
{
	Channel* c = nullptr;
	for (auto& x : state.chan)
	{
		if (x.running)
		{
			if (!x.can_move())
			{
				x.running = false;
				continue;
			}
			c = &x;
			break;
		}
	}
	if (!c)
	{
		sh2.dma_busy = false;
		state.owed = 0;
		return false;
	}
	if (cpu_now() < c->start_time)
	{
		return false;
	}
	if (!c->started)
	{
		c->started = true;
		state.owed = 1;
	}
	bool took = false;
	while (sh2.cycles_left > 0 && c->can_move() && (c->ctrl.is_burst || cpu_idle || state.owed > 0))
	{
		sh2.cycles_left -= c->unit();
		state.owed--;
		took = true;
	}
	//DEI once the last unit's bus cycles are over
	if (c->ctrl.finished)
	{
		c->update_irq((int)(c - state.chan));
	}
	if (!c->can_move())
	{
		c->running = false;
	}
	return took;
}

//When the transfer can start, for a CPU that is waiting (SLEEP)
int64_t next_start()
{
	for (auto& x : state.chan)
	{
		if (x.running)
		{
			return x.start_time;
		}
	}
	return INT64_MAX;
}

void cpu_accessed(int transfers)
{
	for (auto& x : state.chan)
	{
		if (x.running && x.started && !x.ctrl.is_burst)
		{
			state.owed += transfers;
			return;
		}
	}
}

static void check_activations()
{
	//TODO: check NMI and address error flags
	bool master_enable = state.ctrl & 0x1;
	if (!master_enable)
	{
		return;
	}

	for (auto& x : state.chan)
	{
		//An edge-sensed external request moves one unit per edge (dreq0), never
		//on the level
		if (x.ctrl.enable && !x.ctrl.finished && state.dreqs[x.ctrl.mode] && !x.edge_sensed())
		{
			x.activate();
		}
	}
}

//DREQ0: the VDP's raster DMA signal, when PA13 is set to DREQ0 (as the BIOS
//leaves it). With CHCR.DS a channel moves one unit per falling edge; otherwise it
//keeps moving (cycle stealing) while the signal is low.
void dreq0(bool low, bool fell)
{
	state.dreqs[(int)DREQ::External] = low;
	if (!(state.ctrl & 0x1))
	{
		return;
	}
	for (auto& x : state.chan)
	{
		if (x.ctrl.mode != (int)DREQ::External || !x.ctrl.enable || x.ctrl.finished)
		{
			continue;
		}
		if (x.edge_sensed())
		{
			if (fell)
			{
				x.edge_units++;
				x.activate();
			}
		}
		else if (low)
		{
			x.activate();
		}
	}
}

uint16_t read16(uint32_t addr)
{
	addr &= 0x3F;
	if (addr == 0x08)
	{
		return state.ctrl;
	}

	int reg = addr & 0x0F;
	Channel* chan = &state.chan[addr >> 4];
	switch (reg)
	{
	//SAR and DAR read back in halves, all 32 bits
	case 0x00:
		return chan->src_addr >> 16;
	case 0x02:
		return chan->src_addr & 0xFFFF;
	case 0x04:
		return chan->dst_addr >> 16;
	case 0x06:
		return chan->dst_addr & 0xFFFF;
	case 0x0A:
		return chan->transfer_size & 0xFFFF;
	case 0x0E:
		chan->ctrl.finished_read |= chan->ctrl.finished;
		return chan->get_ctrl();
	default:
		LOG_UNEMULATED("[DMAC] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
		return 0;
	}
}

void write16(uint32_t addr, uint16_t value)
{
	addr &= 0x3F;

	//DMAOR's NMIF and AE flags can only be cleared by a write
	if (addr == 0x08)
	{
		state.ctrl = (value & ~0x6u) | (state.ctrl & value & 0x6u);
		return;
	}

	int reg = addr & 0x0F;
	Channel* chan = &state.chan[addr >> 4];
	switch (reg)
	{
	case 0x00:
		chan->src_addr = (chan->src_addr & 0xFFFF) | (uint32_t)value << 16;
		break;
	case 0x02:
		chan->src_addr = (chan->src_addr & 0xFFFF0000u) | value;
		break;
	case 0x04:
		chan->dst_addr = (chan->dst_addr & 0xFFFF) | (uint32_t)value << 16;
		break;
	case 0x06:
		chan->dst_addr = (chan->dst_addr & 0xFFFF0000u) | value;
		break;
	case 0x0A:
		chan->transfer_size = value;
		if (!chan->transfer_size)
		{
			chan->transfer_size = 0x10000;
		}
		break;
	case 0x0E:
		chan->set_ctrl(value);
		//Channels 2 and 3 have no AM/AL/DS bits
		if ((addr >> 4) >= 2)
		{
			chan->ctrl.unk_ack_bits = 0;
		}
		chan->update_irq(addr >> 4);
		check_activations();
		break;
	default:
		LOG_UNEMULATED("[DMAC] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
	}
}

void write32(uint32_t addr, uint32_t value)
{
	addr &= 0x3F;

	int reg = addr & 0x0F;
	Channel* chan = &state.chan[addr >> 4];
	switch (reg)
	{
	case 0x00:
		chan->src_addr = value;
		break;
	case 0x04:
		chan->dst_addr = value;
		break;
	default:
		LOG_UNEMULATED("[DMAC] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
	}
}

void initialize()
{
	state = {};
	in_dma_state = false;

	//Auto mode should always go through
	send_dreq(DREQ::Auto);
}

void send_dreq(DREQ dreq)
{
	state.dreqs[(int)dreq] = true;
	check_activations();
}

void clear_dreq(DREQ dreq)
{
	state.dreqs[(int)dreq] = false;
}

bool is_dma_access()
{
	return in_dma_state;
}

void save_state(SaveState::Snapshot& ss)
{
	ss.begin_section(SaveState::fourcc("DMAC"));
	for (auto& chan : state.chan)
	{
		ss.write(chan.src_addr);
		ss.write(chan.dst_addr);
		ss.write(chan.transfer_size);
		ss.write(chan.ctrl);
		ss.write(chan.running);
		ss.write(chan.started);
		ss.write(chan.start_time);
		ss.write(chan.edge_units);
	}
	ss.write(state.dreqs);
	ss.write(state.ctrl);
	ss.write(state.owed);
}

void load_state(SaveState::Snapshot& ss)
{
	ss.expect_section(SaveState::fourcc("DMAC"));
	for (auto& chan : state.chan)
	{
		ss.read(chan.src_addr);
		ss.read(chan.dst_addr);
		ss.read(chan.transfer_size);
		ss.read(chan.ctrl);
		ss.read(chan.running);
		ss.read(chan.started);
		ss.read(chan.start_time);
		ss.read(chan.edge_units);
	}
	ss.read(state.dreqs);
	ss.read(state.ctrl);
	ss.read(state.owed);
}

}