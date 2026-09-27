#include <log/log.h>

#include <cassert>
#include "core/sh2/peripherals/sh2_intc.h"
#include "core/sh2/peripherals/sh2_pfc.h"
#include "core/sh2/sh2_bus.h"

namespace SH2::OCPM::PFC
{

struct GpioState
{
	uint16_t output[2];
	uint16_t direction[2];
	uint16_t control[4];  //PACR1, PACR2, PBCR1, PBCR2
};

//PACR1 pin functions. The VDP's signals reach the INTC only when their pin is
//set to it: PA13 (raster DMA signal) as IRQ1 or DREQ0, PA12 as IRQ0, PA14 as IRQ2
constexpr uint16_t PA13_MODE_MASK = 0x0C00;
constexpr uint16_t PA13_MODE_IRQ1 = 0x0400;
constexpr uint16_t PA12_MODE_MASK = 0x0300;
constexpr uint16_t PA12_MODE_IRQ0 = 0x0100;
constexpr uint16_t PA14_MODE_MASK = 0x3000;
constexpr uint16_t PA14_MODE_IRQ2 = 0x1000;

static GpioState gpio;

bool pa13_is_dreq0()
{
	return (gpio.control[0] & PA13_MODE_MASK) == PA13_MODE_MASK;
}

static void update_pin_functions()
{
	INTC::set_irq1_pin_enabled((gpio.control[0] & PA13_MODE_MASK) == PA13_MODE_IRQ1);
	INTC::set_irq0_pin_enabled((gpio.control[0] & PA12_MODE_MASK) == PA12_MODE_IRQ0);
	INTC::set_irq2_pin_enabled((gpio.control[0] & PA14_MODE_MASK) == PA14_MODE_IRQ2);
	INTC::set_dreq0_pin_enabled((gpio.control[0] & PA13_MODE_MASK) == PA13_MODE_MASK);
}

uint16_t read_gpio_inputs(int port)
{
	if (port == 0)
	{
		// Port A
		constexpr int pa8_cart_present = 1;  //We don't run without a cartridge so this is always high
		constexpr int pa11_unk = 0;  //Tied hard low on all known boards, BIOS copies to an unknown VDP option
		//PA13 carries the VDP's raster DMA signal, readable as a plain input too
		int pa13_raster = INTC::irq1_line_low() ? 0 : 1;
		return (pa13_raster << 13) | (pa11_unk << 11) | (pa8_cart_present << 8);
	}
	else if (port == 1)
	{
		// Port B
		constexpr int pb1_unk = 1;  //Pulled high in most cartridges
		constexpr int pb3_unk = 1;  //Pulled high in most cartridges
		return (pb3_unk << 3) | (pb1_unk << 1);
	}
	return 0;
}

uint16_t read16(uint32_t addr)
{
	addr &= 0x3F;

	int gpio_port;

	switch(addr)
	{
	case 0x00:
	case 0x02:
		gpio_port = (addr >> 1) & 1;
		{
			uint16_t input = read_gpio_inputs(gpio_port);
			uint16_t output = gpio.output[gpio_port];
			uint16_t direction = gpio.direction[gpio_port];
			return (output & direction) | (input & ~direction);
		}
	case 0x04:
	case 0x06:
		gpio_port = (addr >> 1) & 1;
		return gpio.direction[gpio_port];
	case 0x08:
	case 0x0A:
	case 0x0C:
	case 0x0E:
		return gpio.control[(addr - 0x08) >> 1];
	default:
		Log::warn("[PFC] unmapped read %08X", addr);
		return 0;
	}
}

void write16(uint32_t addr, uint16_t value)
{
	addr &= 0x3F;

	int gpio_port;

	switch(addr)
	{
	case 0x00:
	case 0x02:
		gpio_port = (addr >> 1) & 1;
		gpio.output[gpio_port] = value;
		Log::debug("[PFC] GPIO write P%sDR: %04X", gpio_port ? "B" : "A", value);
		break;
	case 0x04:
	case 0x06:
		gpio_port = (addr >> 1) & 1;
		gpio.direction[gpio_port] = value;
		Log::debug("[PFC] GPIO write P%sIOR: %04X", gpio_port ? "B" : "A", value);
		break;
	case 0x08:
	case 0x0C:
		gpio_port = (addr >> 2) & 1;
		gpio.control[(addr - 0x08) >> 1] = value;
		Log::debug("[PFC] GPIO write P%sCR1: %04X", gpio_port ? "B" : "A", value);
		if (addr == 0x08)
		{
			update_pin_functions();
		}
		break;
	case 0x0A:
	case 0x0E:
		gpio_port = (addr >> 2) & 1;
		gpio.control[(addr - 0x08) >> 1] = value;
		Log::debug("[PFC] GPIO write P%sCR2: %04X", gpio_port ? "B" : "A", value);
		break;
	case 0x2E:
		//CASCR: selects the DRAM column address strobe pins, no emulated effect
		break;
	default:
		Log::warn("[PFC] unmapped write %08X: %04X", addr, value);
	}
}

void initialize()
{
	gpio = {};
}

void save_state(SaveState::Snapshot& ss)
{
	ss.begin_section(SaveState::fourcc("PFC "));
	ss.write(gpio);
}

void load_state(SaveState::Snapshot& ss)
{
	ss.expect_section(SaveState::fourcc("PFC "));
	ss.read(gpio);
}

}  // namespace SH2::OCPM::PFC