#include "core/sh2/peripherals/sh2_wdt.h"

#include <log/log.h>

#include "core/sh2/peripherals/sh2_intc.h"
#include "core/timing.h"

namespace SH2::OCPM::WDT
{

//Watchdog timer: 8-bit TCNT clocked from the free-running prescaler. An overflow
//sets OVF (interval mode, requests ITI) or WOVF (watchdog mode; the chip reset
//with RSTE is not emulated). Stopping it clears TCNT.

constexpr static int DIVIDERS[8] = { 2, 64, 128, 256, 512, 1024, 4096, 8192 };

struct State
{
	bool ovf, wtit, tme;
	int cks;
	bool wovf, rste, rsts;
	//OVF / WOVF read as 1 since they were set: only then does writing 0 clear them
	bool ovf_read, wovf_read;
	//TCNT at base_time; while TME is set it counts from there
	uint8_t tcnt;
	int64_t base_time;
	Timing::EventHandle ev;
};

static State state;
static Timing::FuncHandle ev_func;

static int64_t now()
{
	return Timing::get_timestamp(Timing::CPU_TIMER) - Timing::event_lateness();
}

static int64_t prescaler_edges(int64_t from, int64_t to)
{
	int64_t p = DIVIDERS[state.cks];
	return to / p - from / p;
}

static uint8_t live_tcnt()
{
	if (!state.tme)
	{
		return state.tcnt;
	}
	return (uint8_t)(state.tcnt + prescaler_edges(state.base_time, now()));
}

static void update_irq()
{
	if (state.ovf && !state.wtit)
	{
		INTC::assert_irq(INTC::IRQ::WDT, 0);
	}
	else
	{
		INTC::deassert_irq(INTC::IRQ::WDT);
	}
}

//Take the count to now and schedule the next overflow
static void rebase()
{
	int64_t t = now();
	state.tcnt = live_tcnt();
	state.base_time = t;
	if (state.ev.is_valid())
	{
		Timing::cancel_event(state.ev);
	}
	if (state.tme)
	{
		int64_t p = DIVIDERS[state.cks];
		int64_t overflow_at = (t / p + (256 - state.tcnt)) * p;
		state.ev = Timing::add_event(ev_func, Timing::convert_cpu(overflow_at - Timing::get_timestamp(Timing::CPU_TIMER)), 0,
									 Timing::CPU_TIMER);
	}
}

static void overflow(uint64_t, int)
{
	state.ev = Timing::EventHandle();
	state.tcnt = 0;
	state.base_time = now();
	if (state.wtit)
	{
		state.wovf = true;
		if (state.rste)
		{
			LOG_UNEMULATED("[WDT] watchdog overflow with RSTE set: the chip reset is not emulated");
		}
		else
		{
			//Without RSTE only TCNT and TCSR reset
			state.tme = false;
			state.wtit = false;
			state.cks = 0;
		}
	}
	else
	{
		state.ovf = true;
	}
	update_irq();
	rebase();
}

void initialize()
{
	state = {};
	ev_func = Timing::register_func("WDT::overflow", overflow);
}

uint8_t read8(uint32_t addr)
{
	switch (addr & 3)
	{
	case 0:
		state.ovf_read |= state.ovf;
		return (uint8_t)(state.ovf << 7 | state.wtit << 6 | state.tme << 5 | 0x18 | state.cks);
	case 1:
		return live_tcnt();
	case 3:
		state.wovf_read |= state.wovf;
		return (uint8_t)(state.wovf << 7 | state.rste << 6 | state.rsts << 5 | 0x1F);
	default:
		return 0xFF;
	}
}

uint16_t read16(uint32_t addr)
{
	return (addr & 2) ? read8(3) : (uint16_t)(read8(0) << 8 | read8(1));
}

//Keyed writes: at +0, A5 writes TCSR and 5A TCNT; at +2, A5 clears WOVF and 5A
//writes RSTE/RSTS
void write16(uint32_t addr, uint16_t value)
{
	uint8_t key = value >> 8, v = value & 0xFF;
	if (!(addr & 2))
	{
		if (key == 0xA5)
		{
			state.tcnt = live_tcnt();
			state.base_time = now();
			if (state.ovf_read && !(v & 0x80))
			{
				state.ovf = false;
				state.ovf_read = false;
			}
			state.wtit = (v >> 6) & 1;
			state.tme = (v >> 5) & 1;
			state.cks = v & 7;
			if (!state.tme)
			{
				state.tcnt = 0;
			}
			update_irq();
			rebase();
		}
		else if (key == 0x5A)
		{
			state.tcnt = v;
			state.base_time = now();
			rebase();
		}
		return;
	}
	if (key == 0xA5)
	{
		if (state.wovf_read && !(v & 0x80))
		{
			state.wovf = false;
			state.wovf_read = false;
		}
	}
	else if (key == 0x5A)
	{
		state.rste = (v >> 6) & 1;
		state.rsts = (v >> 5) & 1;
	}
}

void save_state(SaveState::Snapshot& ss)
{
	ss.begin_section(SaveState::fourcc("WDT "));
	ss.write(state);
}

void load_state(SaveState::Snapshot& ss)
{
	ss.expect_section(SaveState::fourcc("WDT "));
	ss.read(state);
}

}
