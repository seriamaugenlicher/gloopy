#include <common/wordops.h>
#include <core/loopy_io.h>
#include <core/timing.h>
#include <input/input.h>
#include <log/log.h>

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace LoopyIO
{

struct PadState
{
	bool plugged;
	uint16_t buttons;
};

struct MouseState
{
	bool plugged;
	uint16_t buttons;
	int16_t counter_x;
	int16_t counter_y;
};

struct State
{
	uint16_t latched_sensors;
	uint16_t print_temp;  //ANALOG_IN as read: the reading in bits 15-6, then adc_ctrl
	uint16_t adc_ctrl;
	PadState pad;
	MouseState mouse;
	bool scan_pad;
	bool scan_mouse;
	uint16_t control_out;  //CONTROL_OUT: the port's output pins in direct mode
	int64_t out5_high_since;  //when output 5 last went high
};

static State state;

//Homebrew debug console: one character per 16-bit store to the unused register
//0x0F0 (0x0C05D0F0), logged a line at a time as "[Console] ...". SCI0's transmit
//is logged the same way as "[Serial] ...".
struct LineLog
{
	const char* tag;
	char line[256];
	size_t len;

	void putc(uint8_t c)
	{
		if (c == '\r')
		{
			return;
		}
		if (c == '\n' || len == sizeof(line) - 1)
		{
			line[len] = 0;
			Log::info("[%s] %s", tag, line);
			len = 0;
			if (c == '\n')
			{
				return;
			}
		}
		line[len++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
	}
};

static LineLog console_log = { "Console" };
static LineLog serial_log = { "Serial" };

static void console_putc(uint8_t c)
{
	console_log.putc(c);
}

void serial0_tx(uint8_t c)
{
	serial_log.putc(c);
}

//Unmapped registers are reported once each: a program polling one every frame
//otherwise buries the log.
static bool unmapped_reported[0x800];

void initialize()
{
	state = {};
	console_log.len = 0;
	serial_log.len = 0;
	for (bool& r : unmapped_reported)
	{
		r = false;
	}
}

void shutdown()
{
	//nop
}

uint8_t reg_read8(uint32_t addr)
{
	READ_HALFWORD(reg, addr);
}

//Direct controller mode with a gamepad and nothing pressed, as measured: outputs 0
//or 1 alone set input 0 in CONTROL_IN[0]'s low byte; all six set it in both bytes,
//the high byte only once output 5 has been high for about 3000 cycles. Button
//presses in direct mode are not emulated.
constexpr int64_t DIRECT_PAD_SETTLE = 3000;

static uint16_t direct_pad_in0()
{
	uint16_t out = state.control_out & 0x3F;
	uint16_t value = (out & 0x03) ? 0x0001 : 0;
	if (out == 0x3F && Timing::get_timestamp(Timing::CPU_TIMER) - state.out5_high_since >= DIRECT_PAD_SETTLE)
	{
		value |= 0x0100;
	}
	return value;
}

uint16_t reg_read16(uint32_t addr)
{
	addr &= 0xFFE;
	switch (addr)
	{
	case 0x000:
		return state.print_temp;
	case 0x010:
		if (state.scan_pad && state.pad.plugged)
		{
			return ((state.pad.buttons << 4) & 0x0F00) | (state.pad.buttons & 0x000E) | 0x0001;
		}
		else if (!state.scan_pad && state.pad.plugged && !state.mouse.plugged)
		{
			return direct_pad_in0();
		}
		else if (state.mouse.plugged)
		{
			uint16_t mb = ((~state.mouse.buttons) & 0x5000) | 0x8000;
			return mb | (mb >> 8);
		}
		return 0;
	case 0x012:
		if (state.scan_pad && state.pad.plugged)
		{
			return (state.pad.buttons >> 8) & 0x000F;
		}
		else if (state.mouse.plugged)
		{
			uint16_t mb = ((~state.mouse.buttons) & 0x5000) | 0x8000;
			return mb | (mb >> 8);
		}
		return 0;
	case 0x014:
		if (state.scan_pad && state.pad.plugged)
		{
			return 0;
		}
		else if (state.mouse.plugged)
		{
			uint16_t mb = ((~state.mouse.buttons) & 0x5000) | 0x8000;
			return mb | (mb >> 8);
		}
		return 0;
	case 0x030:
		return state.latched_sensors;
	//Documented registers with nothing emulated behind them (see reg_write16). They
	//read as 0 without a warning, since retail games touch them routinely.
	case 0x020:
	case 0x040:
	case 0x042:
	case 0x044:
	case 0x054:
		return 0;
	case 0x050:
		if (state.scan_mouse && state.mouse.plugged)
		{
			uint16_t mouse_xreg = state.mouse.counter_x & 0xFFF;
			state.mouse.counter_x = 0;
			//The buttons are active-low: an idle mouse reads 5000 on a console
			mouse_xreg |= (~state.mouse.buttons & (Input::MOUSE_L | Input::MOUSE_R));
			//With the pad scanned too, the counters stay 0 on a console
			if (state.scan_pad)
			{
				mouse_xreg &= 0xF000;
			}
			return mouse_xreg;
		}
		return 0;
	case 0x052:
		if (state.scan_mouse && state.mouse.plugged)
		{
			uint16_t mouse_yreg = state.mouse.counter_y & 0xFFF;
			state.mouse.counter_y = 0;
			return state.scan_pad ? 0 : mouse_yreg;
		}
		return 0;
	default:
		Log::warn("[IO] unmapped read16 %08X", addr);
		return 0;
	}
}

uint32_t reg_read32(uint32_t addr)
{
	READ_DOUBLEWORD(reg, addr);
}

void reg_write8(uint32_t addr, uint8_t value)
{
	WRITE_HALFWORD(reg, addr, value);
}

void reg_write16(uint32_t addr, uint16_t value)
{
	addr &= 0xFFE;
	switch (addr)
	{
	case 0x030:
		state.latched_sensors = (state.latched_sensors & ~0x0100) | (value & 0x0100);
		//Without this break the sensor latch fell through into the default case, so
		//every legitimate write to it was reported as an unmapped one
		break;
	//Documented, accepted and ignored (retail games write them):
	//  020 EXP_TIMING     expansion strobe timing, which has no emulated effect
	//  040-044            print head data, motor and head control; printing is done
	//                     by a BIOS hook, so the mechanism's drive signals do nothing
	case 0x000:
		//Channel select (bits 5-3) and auto-sampling control (bits 2-0, not
		//emulated); the reading changes on the next conversion
		state.adc_ctrl = value & 0x3F;
		state.print_temp = (state.print_temp & 0xFFC0) | state.adc_ctrl;
		break;
	case 0x054:
		if ((value & 0x20) && !(state.control_out & 0x20))
		{
			state.out5_high_since = Timing::get_timestamp(Timing::CPU_TIMER);
		}
		state.control_out = value & 0x3F;
		break;
	case 0x020:
	case 0x040:
	case 0x042:
	case 0x044:
		break;
	case 0x0F0:
		console_putc((uint8_t)value);
		break;
	default:
		if (!unmapped_reported[addr >> 1])
		{
			unmapped_reported[addr >> 1] = true;
			Log::warn("[IO] unmapped write16 %08X: %04X (not reported again)", addr, value);
		}
	}
}

void reg_write32(uint32_t addr, uint32_t value)
{
	WRITE_DOUBLEWORD(reg, addr, value);
}

void update_pad(int btn_info, bool pressed)
{
	if (state.pad.plugged)
	{
		if (pressed)
		{
			state.pad.buttons |= btn_info;
		}
		else
		{
			state.pad.buttons &= ~btn_info;
		}
	}
}

void update_mouse_buttons(int btn_info, bool pressed)
{
	if (state.mouse.plugged)
	{
		if (pressed)
		{
			state.mouse.buttons |= btn_info;
		}
		else
		{
			state.mouse.buttons &= ~btn_info;
		}
	}
}

void update_mouse_position(int delta_x, int delta_y)
{
	if (state.mouse.plugged)
	{
		state.mouse.counter_x = std::clamp(state.mouse.counter_x + delta_x, -2048, 2047);
		state.mouse.counter_y = std::clamp(state.mouse.counter_y + delta_y, -2048, 2047);
	}
}

void set_controller_scan_mode(bool scan_pad, bool scan_mouse)
{
	state.scan_pad = scan_pad;
	state.scan_mouse = scan_mouse;
}

void set_controller_plugged(bool plugged_pad, bool plugged_mouse)
{
	state.pad.plugged = plugged_pad && !plugged_mouse;
	state.mouse.plugged = plugged_mouse;

	if (!plugged_mouse)
	{
		state.mouse.counter_x = 0;
		state.mouse.counter_y = 0;
	}
}

void save_state(SaveState::Snapshot& ss)
{
	ss.begin_section(SaveState::fourcc("LPIO"));
	ss.write(state.latched_sensors);
	ss.write(state.print_temp);
	ss.write(state.adc_ctrl);
	ss.write(state.scan_pad);
	ss.write(state.control_out);
	ss.write(state.out5_high_since);
	ss.write(state.scan_mouse);
	ss.write(state.pad);
	ss.write(state.mouse);
}

void load_state(SaveState::Snapshot& ss)
{
	ss.expect_section(SaveState::fourcc("LPIO"));
	ss.read(state.latched_sensors);
	ss.read(state.print_temp);
	ss.read(state.adc_ctrl);
	ss.read(state.scan_pad);
	ss.read(state.control_out);
	ss.read(state.out5_high_since);
	ss.read(state.scan_mouse);

	//Controller plugged/button state reflects the live inputs of this session,
	//not the moment of the save - read and discard
	PadState saved_pad;
	MouseState saved_mouse;
	ss.read(saved_pad);
	ss.read(saved_mouse);
}

//A conversion of the selected ADC channel, with readings from a console at room
//temperature: head thermistor, head calibration resistor, contrast knob and the
//two cartridge inputs. Channels 5-7 select channel 4.
void update_print_temp()
{
	static const uint16_t readings[5] = { 0x14A, 0x234, 0x205, 0x003, 0x002 };
	int ch = std::min((state.adc_ctrl >> 3) & 0x7, 4);
	state.print_temp = (uint16_t)(readings[ch] << 6) | state.adc_ctrl;
}

void update_sensors()
{
	//Stock mainboard is always configured for NTSC
	constexpr int region_jumper = 1;

	//BIOS hooks allow simulated printing of XS-11 type seals
	constexpr bool seal_cartridge_present = true;
	constexpr int seal_cartridge_type = 1;

	//Set sensors for appropriate idle state
	int print_mech_sensors = seal_cartridge_present ? 0b100 : 0b011;

	state.latched_sensors = (state.latched_sensors & 0x0100) | ((seal_cartridge_type & 7) << 4) | ((print_mech_sensors & 7) << 1) | (region_jumper & 1);
}

}  // namespace LoopyIO