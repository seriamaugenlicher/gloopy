#include "core/sh2/peripherals/sh2_timers.h"

#include <log/log.h>

#include <cassert>
#include <cstdio>
#include <tuple>

#include "core/sh2/peripherals/sh2_intc.h"
#include "core/timing.h"

namespace SH2::OCPM::Timer
{

constexpr static int TIMER_COUNT = 5;

//TCNT starts counting 2 cycles after the TSTR write that starts it (measured)
constexpr static int START_DELAY_CYCLES = 2;

static Timing::FuncHandle ev_func;

struct Timer
{
	Timing::EventHandle ev;
	INTC::IRQ irq;
	int enabled;
	int id;

	struct Ctrl
	{
		int clock;
		int edge_mode;
		int clear_mode;
	};

	Ctrl ctrl;

	int intr_enable;
	int intr_flag;
	//Flags read as 1 since they were set: only those clear when written 0
	int intr_flag_read;
	//TCR, TIOR and TIER as written, for readback (pin compare/capture not emulated)
	int ctrl_raw;
	int io_ctrl;
	int intr_enable_raw;

	uint32_t counter;
	uint32_t counter_when_started;
	uint32_t gen_reg[2];

	int64_t time_when_started;

	//The counter now; inside a timer event, at the time the event was due
	void update_counter()
	{
		if (!ev.is_valid())
		{
			return;
		}

		int64_t time_elapsed = Timing::read_time() - Timing::event_lateness() - time_when_started;
		//Not counting yet: still inside the start delay
		if (time_elapsed < 0)
		{
			time_elapsed = 0;
		}
		counter = counter_when_started + (time_elapsed >> (ctrl.clock & 3));
		counter &= 0xFFFF;
	}

	//Flags a TSR read sees from the cycle after the match, like TCNT, even inside
	//the instruction whose timer event has not run yet (measured)
	void flags_due()
	{
		if (!ev.is_valid())
		{
			return;
		}
		int64_t now = Timing::read_time();
		uint32_t targets[3] = { gen_reg[0], gen_reg[1], 0x10000 };
		for (int i = 0; i < 3; i++)
		{
			if (counter_when_started < targets[i])
			{
				int64_t match = time_when_started + ((int64_t)(targets[i] - counter_when_started) << (ctrl.clock & 3));
				if (now >= match + 1)
				{
					intr_flag |= 1 << i;
				}
			}
		}
	}

	void set_enable(bool new_enable, int delay = 0)
	{
		enabled = new_enable;

		if (!ev.is_valid() && enabled)
		{
			start(delay);
		}
		else if (ev.is_valid() && !enabled)
		{
			Timing::cancel_event(ev);
		}
	}

	void start(int delay = 0)
	{
		if (ctrl.clock & ~0x3)
		{
			LOG_UNEMULATED("[Timer] ITU external clock input (TCR TPSC %d) is not emulated: counting at phi/%d", ctrl.clock, 1 << (ctrl.clock & 3));
		}
		if (ctrl.clear_mode == 3)
		{
			LOG_UNEMULATED("[Timer] ITU synchronous clearing (TCR CCLR 3) is not emulated");
		}

		//Calculate the target which will take the smallest amount of time to reach
		constexpr static uint32_t OVERFLOW_TARGET = 0x10000;
		uint32_t nearest_target = OVERFLOW_TARGET;
		for (int i = 0; i < 2; i++)
		{
			if (counter < gen_reg[i])
			{
				nearest_target = std::min(nearest_target, gen_reg[i]);
			}
		}

		//The timer index (not a pointer) is used as the event param so save states can serialize it
		//Counting begins now, or after the start delay if still inside it
		int64_t now = Timing::get_timestamp(Timing::CPU_TIMER);
		int64_t begin = now - Timing::event_lateness() + delay;
		if (time_when_started > begin)
		{
			begin = time_when_started;
		}
		uint32_t cycles = (nearest_target - counter) << (ctrl.clock & 3);
		Timing::UnitCycle sched_cycles = Timing::convert_cpu(std::max<int64_t>((int64_t)cycles + (begin - now), 0));
		ev = Timing::add_event(ev_func, sched_cycles, (uint64_t)id, Timing::CPU_TIMER);

		time_when_started = begin;
		counter_when_started = counter;
	}
};

struct State
{
	int timer_enable;
	int sync_ctrl;
	int mode;
	int fc_ctrl;
	//Word reads of a byte register pair return the last byte written in the low half
	uint8_t write_latch;

	Timer timers[TIMER_COUNT];
};

typedef std::tuple<Timer*, int> TimerDev;

static State state;

static void update_timer_irq(Timer* timer)
{
	int subirq = -1;
	for (int i = 0; i < 3; i++)
	{
		if (timer->intr_enable & timer->intr_flag & (1 << i))
		{
			subirq = i;
			break;
		}
	}

	if (subirq >= 0)
	{
		INTC::assert_irq(timer->irq, subirq);
	}
	else
	{
		INTC::deassert_irq(timer->irq);
	}
}

static void update_timer_target(Timer* timer)
{
	// Disable and re-enable to force new timing to take effect
	if (timer->enabled)
	{
		timer->set_enable(false);
		timer->set_enable(true);
	}
}

static void intr_event(uint64_t param, int cycles_late)
{
	Timer* timer = &state.timers[param % TIMER_COUNT];

	timer->update_counter();

	bool clear_counter = false;

	//Compare 1
	if (timer->counter == timer->gen_reg[0])
	{
		timer->intr_flag |= 0x1;
		if (timer->ctrl.clear_mode == 0x1)
		{
			clear_counter = true;
		}
	}

	//Compare 2
	if (timer->counter == timer->gen_reg[1])
	{
		timer->intr_flag |= 0x2;
		if (timer->ctrl.clear_mode == 0x2)
		{
			clear_counter = true;
		}
	}

	//Overflow
	if (timer->counter == 0)
	{
		timer->intr_flag |= 0x4;
	}

	if (clear_counter)
	{
		timer->counter = 0;
	}

	update_timer_irq(timer);

	//Restart the timer
	timer->start();
}

static TimerDev get_dev_from_addr(uint32_t addr)
{
	addr &= 0x3F;

	//Timers 3 and 4 have extra registers and are also spaced oddly
	if (addr >= 0x32)
	{
		return TimerDev(&state.timers[4], addr - 0x32);
	}

	if (addr >= 0x22 && addr < 0x30)
	{
		return TimerDev(&state.timers[3], addr - 0x22);
	}

	//The remaining timers have predictable spacing
	if (addr >= 0x04 && addr < 0x22)
	{
		addr -= 0x04;
		int id = addr / 0xA;
		int reg = addr % 0xA;
		return TimerDev(&state.timers[id], reg);
	}

	//Shared registers don't have a timer pointer
	return TimerDev(nullptr, addr);
}

void initialize()
{
	state = {};
	ev_func = {};

	for (int i = 0; i < TIMER_COUNT; i++)
	{
		state.timers[i].id = i;
	}

	state.timers[0].irq = INTC::IRQ::ITU0;
	state.timers[1].irq = INTC::IRQ::ITU1;
	state.timers[2].irq = INTC::IRQ::ITU2;
	state.timers[3].irq = INTC::IRQ::ITU3;
	state.timers[4].irq = INTC::IRQ::ITU4;

	ev_func = Timing::register_func("Timer::intr_event", intr_event);
}

uint8_t read8(uint32_t addr)
{
	TimerDev dev = get_dev_from_addr(addr);

	Timer* timer = std::get<Timer*>(dev);
	int reg = std::get<int>(dev);

	if (timer)
	{
		//Readback as measured: TIOR bit 3, TIER bits 6-3 and TSR bits 6-3 read as 1
		switch (reg)
		{
		case 0x00:
			return timer->ctrl_raw;
		case 0x01:
			return 0x08 | timer->io_ctrl;
		case 0x02:
			return 0x78 | timer->intr_enable_raw;
		case 0x03:
			timer->flags_due();
			timer->intr_flag_read |= timer->intr_flag;
			return timer->intr_flag | 0x78;
		case 0x04:
		case 0x05:
			timer->update_counter();
			return (uint8_t)(reg == 0x04 ? timer->counter >> 8 : timer->counter);
		case 0x06:
		case 0x07:
		case 0x08:
		case 0x09:
		{
			uint32_t gr = timer->gen_reg[(reg - 0x06) >> 1];
			return (uint8_t)((reg & 1) ? gr : gr >> 8);
		}
		default:
			LOG_UNEMULATED("[Timer] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
			return 0;
		}
	}

	switch (reg)
	{
	case 0x00:
		return state.timer_enable | 0x60;
	case 0x01:
		return state.sync_ctrl | 0x60;
	case 0x02:
		return state.mode;
	case 0x03:
		return state.fc_ctrl | 0x40;
	default:
		LOG_UNEMULATED("[Timer] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
		return 0;
	}
}

uint16_t read16(uint32_t addr)
{
	TimerDev dev = get_dev_from_addr(addr);

	Timer* timer = std::get<Timer*>(dev);
	int reg = std::get<int>(dev);

	if (timer)
	{
		switch (reg)
		{
		case 0x04:
			//TCNT is a live free-running counter on the real SH7021; derive it
			//from the elapsed time so software may poll it (e.g. for a clock)
			timer->update_counter();
			return (uint16_t)timer->counter;
		case 0x06:
		case 0x08:
			return (uint16_t)timer->gen_reg[(reg - 0x06) >> 1];
		default:
			break;
		}
	}

	//A word read of a byte register pair: the even one, then the write latch
	return (uint16_t)(read8(addr & ~1u) << 8 | state.write_latch);
}

void write8(uint32_t addr, uint8_t value)
{
	state.write_latch = value;
	TimerDev dev = get_dev_from_addr(addr);

	Timer* timer = std::get<Timer*>(dev);
	int reg = std::get<int>(dev);

	if (timer)
	{
		switch (reg)
		{
		case 0x00:
			Log::debug("[Timer] write timer%d ctrl: %02X", timer->id, value);
			timer->ctrl_raw = value;
			timer->update_counter();
			timer->ctrl.clock = value & 0x7;
			timer->ctrl.edge_mode = (value >> 3) & 0x3;
			timer->ctrl.clear_mode = (value >> 5) & 0x3;
			update_timer_target(timer);
			break;
		case 0x01:
			Log::debug("[Timer] write timer%d io ctrl: %02X", timer->id, value);
			timer->io_ctrl = value & 0xF7;
			break;
		case 0x02:
			Log::debug("[Timer] write timer%d intr enable: %02X", timer->id, value);
			timer->intr_enable = value & 0x7;
			timer->intr_enable_raw = value & 0x87;
			update_timer_irq(timer);
			break;
		case 0x03:
		{
			Log::debug("[Timer] write timer%d intr flag: %02X", timer->id, value);
			int clear = timer->intr_flag_read & ~value;
			timer->intr_flag &= ~clear;
			timer->intr_flag_read &= ~clear;
			update_timer_irq(timer);
			break;
		}
		case 0x04:
			Log::debug("[Timer] write timer%d counter: %02X**", timer->id, value);
			//The BIOS writes 0 to here under the assumption that it resets the whole counter...
			timer->update_counter();
			timer->counter &= 0x00FF;
			timer->counter |= value << 8;
			update_timer_target(timer);
			break;
		case 0x05:
			Log::debug("[Timer] write timer%d counter: **%02X", timer->id, value);
			timer->update_counter();
			timer->counter &= 0xFF00;
			timer->counter |= value;
			update_timer_target(timer);
			break;
		default:
			LOG_UNEMULATED("[Timer] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
		}

		return;
	}

	switch (reg)
	{
	case 0x00:
		Log::debug("[Timer] write master enable: %02X", value);
		state.timer_enable = value & 0x1F;

		for (int i = 0; i < TIMER_COUNT; i++)
		{
			state.timers[i].set_enable((value >> i) & 0x1, START_DELAY_CYCLES);
		}
		break;
	case 0x01:
		Log::debug("[Timer] write sync ctrl: %02X", value);
		state.sync_ctrl = value & 0x9F;
		if (state.sync_ctrl & 0x1F)
		{
			LOG_UNEMULATED("[Timer] ITU synchronous operation (TSNC %02X) is not emulated", state.sync_ctrl);
		}
		break;
	case 0x02:
		Log::debug("[Timer] write mode: %02X", value);
		state.mode = value;
		if (state.mode & 0x7F)
		{
			LOG_UNEMULATED("[Timer] ITU PWM / phase counting modes (TMDR %02X) are not emulated", state.mode);
		}
		break;
	case 0x03:
		Log::debug("[Timer] write TFCR: %02X", value);
		state.fc_ctrl = value & 0xBF;
		if (value & 0x3F)
		{
			LOG_UNEMULATED("[Timer] ITU complementary / reset-synchronised PWM (TFCR %02X) is not emulated", value);
		}
		break;
	default:
		LOG_UNEMULATED("[Timer] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
	}
}

void save_state(SaveState::Snapshot& ss)
{
	ss.begin_section(SaveState::fourcc("ITU "));
	ss.write(state.timer_enable);
	ss.write(state.sync_ctrl);
	ss.write(state.mode);
	ss.write(state.fc_ctrl);
	ss.write(state.write_latch);
	for (auto& timer : state.timers)
	{
		ss.write(timer.ev.value);
		ss.write(timer.enabled);
		ss.write(timer.ctrl);
		ss.write(timer.intr_enable);
		ss.write(timer.intr_flag);
		ss.write(timer.intr_flag_read);
		ss.write(timer.ctrl_raw);
		ss.write(timer.io_ctrl);
		ss.write(timer.intr_enable_raw);
		ss.write(timer.counter);
		ss.write(timer.counter_when_started);
		ss.write(timer.gen_reg);
		ss.write(timer.time_when_started);
	}
}

void load_state(SaveState::Snapshot& ss)
{
	ss.expect_section(SaveState::fourcc("ITU "));
	ss.read(state.timer_enable);
	ss.read(state.sync_ctrl);
	ss.read(state.mode);
	ss.read(state.fc_ctrl);
	ss.read(state.write_latch);
	for (auto& timer : state.timers)
	{
		//The scheduled event itself is restored by Timing::load_state; the handle stays
		//consistent because event ids are preserved
		ss.read(timer.ev.value);
		ss.read(timer.enabled);
		ss.read(timer.ctrl);
		ss.read(timer.intr_enable);
		ss.read(timer.intr_flag);
		ss.read(timer.intr_flag_read);
		ss.read(timer.ctrl_raw);
		ss.read(timer.io_ctrl);
		ss.read(timer.intr_enable_raw);
		ss.read(timer.counter);
		ss.read(timer.counter_when_started);
		ss.read(timer.gen_reg);
		ss.read(timer.time_when_started);
	}
}

void write16(uint32_t addr, uint16_t value)
{
	TimerDev dev = get_dev_from_addr(addr);

	Timer* timer = std::get<Timer*>(dev);
	int reg = std::get<int>(dev);

	if (timer)
	{
		switch (reg)
		{
		case 0x04:
			Log::debug("[Timer] write timer%d counter: %04X", timer->id, value);
			timer->counter = value;
			update_timer_target(timer);
			break;
		case 0x06:
		case 0x08:
			reg = (reg - 0x06) >> 1;
			Log::debug("[Timer] write timer%d general reg%d: %04X", timer->id, reg, value);
			timer->update_counter();
			timer->gen_reg[reg] = value;
			update_timer_target(timer);
			break;
		default:
			LOG_UNEMULATED("[Timer] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
		}

		return;
	}

	switch (reg)
	{
	default:
		LOG_UNEMULATED("[Timer] %s: register %03X is not emulated", __func__, (unsigned)(addr & 0xFFF));
	}
}

}  // namespace SH2::OCPM::Timer