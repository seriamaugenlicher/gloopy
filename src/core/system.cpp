#include "core/system.h"

#include <expansion/expansion.h>
#include <input/input.h>
#include <log/log.h>
#include <sound/sound.h>
#include <video/video.h>
#include <printer/printer.h>

#include <exception>

#include "core/cart.h"
#include "core/loopy_io.h"
#include "core/memory.h"
#include "core/savestate.h"
#include "core/sh2/peripherals/sh2_dmac.h"
#include "core/sh2/peripherals/sh2_intc.h"
#include "core/sh2/peripherals/sh2_ocpm.h"
#include "core/sh2/peripherals/sh2_pfc.h"
#include "core/sh2/peripherals/sh2_serial.h"
#include "core/sh2/peripherals/sh2_timers.h"
#include "core/sh2/sh2.h"
#include "core/timing.h"

namespace System
{

void initialize(Config::SystemInfo& config)
{
	//Memory must initialize first
	Memory::initialize(config.bios_rom);

	//Ensure that timing initializes before any CPUs
	Timing::initialize();

	//Initialize CPUs
	SH2::initialize();

	//Initialize core hardware
	Cart::initialize(config.cart);
	LoopyIO::initialize();

	//Initialize subprojects after everything else
	Input::initialize();
	Video::initialize();
	Sound::initialize(config.sound_rom);
	Expansion::initialize(config.cart);
	Printer::initialize(config);

	//Hook up connections between modules
	SH2::OCPM::Serial::set_tx_callback(1, &Sound::midi_byte_in);
}

void shutdown(Config::SystemInfo& config)
{
	//Shutdown all components in the reverse order they were initialized
	Printer::shutdown();
	Expansion::shutdown();
	Sound::shutdown();
	Video::shutdown();
	Input::shutdown();

	LoopyIO::shutdown();
	Cart::shutdown(config.cart);

	SH2::shutdown();

	Timing::shutdown();
	Memory::shutdown();
}

void run()
{
	//Run an entire frame of emulation, stopping when the VDP reaches VSYNC
	Video::start_frame();

	while (!Video::check_frame_end())
	{
		//TODO: if multiple cores are added, ensure that they are relatively synced

		//Calculate the smallest timeslice between all cores
		int64_t slice_length = (std::numeric_limits<int64_t>::max)();
		for (int i = 0; i < Timing::NUM_TIMERS; i++)
		{
			slice_length = std::min(slice_length, Timing::calc_slice_length(i));
		}

		//Run all cores, processing any scheduler events that happen for them
		for (int i = 0; i < Timing::NUM_TIMERS; i++)
		{
			Timing::process_slice(i, slice_length);
		}
	}
}

uint16_t* get_display_output()
{
	return Video::get_display_output();
}

constexpr static uint32_t SAVE_STATE_MAGIC = SaveState::fourcc("LMSE");

//Version 2: full uPD937 sound engine state is serialized (voices,
//instruments, envelopes, MIDI queue) so audio resumes exactly on load
constexpr static uint32_t SAVE_STATE_VERSION = 2;

void save_state(SaveState::Snapshot& ss)
{
	//Save states are only taken between frames (from the frontend event loop),
	//when no CPU timeslice is in progress
	ss.write(SAVE_STATE_MAGIC);
	ss.write(SAVE_STATE_VERSION);

	Cart::save_state(ss);
	Memory::save_state(ss);
	SH2::save_state(ss);
	SH2::OCPM::save_state(ss);
	SH2::OCPM::INTC::save_state(ss);
	SH2::OCPM::DMAC::save_state(ss);
	SH2::OCPM::PFC::save_state(ss);
	SH2::OCPM::Serial::save_state(ss);
	SH2::OCPM::Timer::save_state(ss);
	Timing::save_state(ss);
	Video::save_state(ss);
	LoopyIO::save_state(ss);
	Sound::save_state(ss);
	Expansion::save_state(ss);
}

bool save_state(const std::string& path)
{
	SaveState::Snapshot ss;
	save_state(ss);

	if (!ss.save_file(path))
	{
		Log::error("[System] failed to write save state to %s", path.c_str());
		return false;
	}
	return true;
}

bool load_state(SaveState::Snapshot& ss)
{
	try
	{
		uint32_t magic, version;
		ss.read(magic);
		ss.read(version);
		if (magic != SAVE_STATE_MAGIC)
		{
			Log::error("[System] not a LoopyMSE save state");
			return false;
		}
		if (version != SAVE_STATE_VERSION)
		{
			Log::error("[System] save state version %d not supported", version);
			return false;
		}

		//Cart is first: it verifies the state belongs to the loaded ROM before
		//any emulation state has been modified
		Cart::load_state(ss);
		Memory::load_state(ss);
		SH2::load_state(ss);
		SH2::OCPM::load_state(ss);
		SH2::OCPM::INTC::load_state(ss);
		SH2::OCPM::DMAC::load_state(ss);
		SH2::OCPM::PFC::load_state(ss);
		SH2::OCPM::Serial::load_state(ss);
		SH2::OCPM::Timer::load_state(ss);
		Timing::load_state(ss);
		Video::load_state(ss);
		LoopyIO::load_state(ss);
		Sound::load_state(ss);
		Expansion::load_state(ss);
	}
	catch (const std::exception& e)
	{
		Log::error("[System] error loading save state: %s", e.what());
		return false;
	}

	return true;
}

bool load_state(const std::string& path)
{
	SaveState::Snapshot ss;
	if (!ss.load_file(path))
	{
		Log::error("[System] failed to read save state from %s", path.c_str());
		return false;
	}

	return load_state(ss);
}

}  // namespace System