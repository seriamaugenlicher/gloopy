/*
Gloopy - a Casio Loopy libretro core.

A modified version of LoopyMSE, where the emulation in src/ comes from.
Modified in 2026; no copyright is claimed over the modifications.

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, version 3. It is distributed WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details, in LICENSE.

Everything frontend-related lives in this file. See README.md for the changes
this fork makes, and NOTICES.md for attribution.
*/

#include <libretro.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

#include <common/bswp.h>
#include <core/cart.h>
#include <core/config.h>
#include <core/loopy_io.h>
#include <core/memory.h>
#include <core/system.h>
#include <core/sh2/sh2.h>
#include <core/sh2/sh2_bus.h>
#include <core/sh2/sh2_interpreter.h>
#include <core/timing.h>
#include <imgwriter/imgwriter.h>
#include <input/input.h>
#include <log/log.h>
#include <printer/printer.h>
#include <sound/sound.h>
#include <video/video.h>

#include "libretro_core_options.h"

#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif

#define CORE_NAME "Gloopy"
#define CORE_VERSION "1.2.0" GIT_VERSION

static constexpr int AUDIO_FRAMES_PER_VIDEO_FRAME = Sound::SAMPLES_PER_FRAME;

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;

static Config::SystemInfo config;
static bool game_loaded;

static std::string system_dir;
static std::string save_dir;

static int16_t audio_buffer[AUDIO_FRAMES_PER_VIDEO_FRAME * 2];
static unsigned current_display_height;

//Core options
static bool opt_crop_overscan = true;
static bool opt_printer = true;

//Image format for printed seals. PNG by default: it is what frontends save their
//screenshots as, and a seal is flat pixel art, which PNG stores exactly and
//compresses to a fraction of a BMP.
static int opt_seal_format = ImageWriter::IMAGE_TYPE_PNG;

//Which device is in the Loopy's one controller port. An explicit choice, not
//auto-swapping: 11 of the 13 retail titles pick their device at boot and never look
//again. Mouse and Virtual Mouse both plug in the Loopy Mouse, driven by a real mouse
//or by the gamepad.
enum InputDevice
{
	INPUT_CONTROLLER,
	INPUT_MOUSE,
	INPUT_VIRTUAL_MOUSE,
};
static InputDevice opt_input_device = INPUT_CONTROLLER;

//Scales raw mouse movement. 1x is 3/8 of the raw movement for the virtual mouse and
//3/16 for a real one (the Loopy's ball mouse was low-resolution); both are exact in
//binary, and 2x is twice the default.
constexpr static float VIRTUAL_MOUSE_BASE_SCALE = 0.375f;
constexpr static float REAL_MOUSE_BASE_SCALE = 0.1875f;
static float opt_mouse_multiplier = 1.0f;

//In Virtual Mouse mode either stick or the d-pad moves the cursor; A/L1/L2 left
//click and B/R1/R2 right click (Loopy face-button naming: A south, B east).
//Deadzone filters stick centering drift; max speed is the raw mouse-delta-
//equivalent contributed per frame at full deflection (or a held d-pad direction),
//before the sensitivity scale is applied.
constexpr static int16_t ANALOG_MOUSE_DEADZONE = 6000;
constexpr static int ANALOG_MOUSE_MAX_SPEED = 12;

//Frameskip. The emulated CPU always runs; only VDP compositing and the frame
//blit are dropped, so game logic and audio are untouched. 0 = off, otherwise
//the maximum number of frames that may be skipped in a row.
static unsigned opt_frameskip_max = 0;
static bool opt_frameskip_auto = false;
static unsigned frames_skipped_in_a_row = 0;

//Fed by the frontend when frameskip 'auto' is active
static bool audio_underrun_likely = false;
static bool audio_buffer_status_active = false;

//Which device is plugged in now (the hardware state; opt_input_device is what the
//player asked for). Only Loopy Town and Lupiton's Wonder Palette re-select their
//device after boot, so a change reaches the rest only on restart.
static bool mouse_active = false;

//Real content path, for finding the expansion PCM samples and a BIOS beside the ROM
static std::string content_path;

static size_t serialize_size_cache;

/* ---- Logging ---------------------------------------------------------- */

//Debug Log File (option loopy_debug_log): the session's log, each line stamped with the
//frame count and emulated time, flushed as written so it survives a crash or a kill.
static bool opt_debug_log = false;
static FILE* session_log = nullptr;
static bool crash_dumped = false;
static unsigned frame_counter;

static void session_log_line(const char* level, const char* message)
{
	const double seconds = frame_counter / Timing::frame_rate();
	const unsigned total_tenths = (unsigned)(seconds * 10.0);
	fprintf(session_log, "[%8u %3u:%02u.%u] %-5s %s\n", frame_counter, total_tenths / 600,
			(total_tenths / 10) % 60, total_tenths % 10, level, message);
	fflush(session_log);
}

static void log_sink(Log::Level level, const char* message)
{
	if (session_log)
	{
		static const char* const names[] = {"VERB", "TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
		session_log_line(names[level <= Log::ERROR ? level : Log::ERROR], message);
	}
	if (!log_cb)
	{
		return;
	}

	retro_log_level retro_level;
	switch (level)
	{
	case Log::VERBOSE:
	case Log::TRACE:
	case Log::DEBUG:
		retro_level = RETRO_LOG_DEBUG;
		break;
	case Log::INFO:
		retro_level = RETRO_LOG_INFO;
		break;
	case Log::WARN:
		retro_level = RETRO_LOG_WARN;
		break;
	case Log::ERROR:
	default:
		retro_level = RETRO_LOG_ERROR;
		break;
	}
	log_cb(retro_level, "%s\n", message);
}

/* ---- Input ------------------------------------------------------------ */

struct PadMapping
{
	unsigned retro_id;
	Input::PadButton loopy_button;
};

static const PadMapping PAD_MAPPINGS[] = {
	{RETRO_DEVICE_ID_JOYPAD_UP, Input::PAD_UP},
	{RETRO_DEVICE_ID_JOYPAD_DOWN, Input::PAD_DOWN},
	{RETRO_DEVICE_ID_JOYPAD_LEFT, Input::PAD_LEFT},
	{RETRO_DEVICE_ID_JOYPAD_RIGHT, Input::PAD_RIGHT},
	{RETRO_DEVICE_ID_JOYPAD_B, Input::PAD_A},
	{RETRO_DEVICE_ID_JOYPAD_A, Input::PAD_B},
	{RETRO_DEVICE_ID_JOYPAD_Y, Input::PAD_C},
	{RETRO_DEVICE_ID_JOYPAD_X, Input::PAD_D},
	{RETRO_DEVICE_ID_JOYPAD_L, Input::PAD_L1},
	{RETRO_DEVICE_ID_JOYPAD_R, Input::PAD_R1},
	{RETRO_DEVICE_ID_JOYPAD_START, Input::PAD_START},
};

constexpr static size_t PAD_MAPPING_COUNT = sizeof(PAD_MAPPINGS) / sizeof(PAD_MAPPINGS[0]);

//Only the device in the port is advertised, so the remap screen shows only
//controls that do something
static const struct retro_input_descriptor CONTROLLER_DESCRIPTORS[] = {
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "D-Pad Up"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "D-Pad Down"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "D-Pad Left"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "A"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "B"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "C"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "D"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start"},
	{0, 0, 0, 0, NULL},
};

//With the Loopy Mouse driven by a real mouse, the gamepad does nothing at all
static const struct retro_input_descriptor MOUSE_DESCRIPTORS[] = {
	{0, 0, 0, 0, NULL},
};

//With the virtual mouse the Loopy has no pad to read, so the gamepad's only job
//is to stand in for the mouse itself
static const struct retro_input_descriptor VIRTUAL_MOUSE_DESCRIPTORS[] = {
	{0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Mouse Cursor X"},
	{0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Mouse Cursor Y"},
	{0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Mouse Cursor X"},
	{0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Mouse Cursor Y"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Mouse Up"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Mouse Down"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Mouse Left"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Mouse Right"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Mouse Left Click"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "Mouse Left Click"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "Mouse Left Click"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Mouse Right Click"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "Mouse Right Click"},
	{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "Mouse Right Click"},
	{0, 0, 0, 0, NULL},
};

static void set_input_descriptors()
{
	const struct retro_input_descriptor* descriptors = CONTROLLER_DESCRIPTORS;
	if (opt_input_device == INPUT_MOUSE)
	{
		descriptors = MOUSE_DESCRIPTORS;
	}
	else if (opt_input_device == INPUT_VIRTUAL_MOUSE)
	{
		descriptors = VIRTUAL_MOUSE_DESCRIPTORS;
	}

	environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void*)descriptors);
}

//Sub-pixel remainder of the sensitivity scaling, carried between frames
static float mouse_carry_x = 0.0f;
static float mouse_carry_y = 0.0f;

//Plug a device into the port. The pad and the mouse are mutually exclusive:
//there is only one port, and a game reads it to decide which one is active.
static void plug_device(bool mouse)
{
	if (mouse == mouse_active)
	{
		return;
	}
	mouse_active = mouse;
	mouse_carry_x = 0.0f;
	mouse_carry_y = 0.0f;
	LoopyIO::set_controller_plugged(true, mouse_active);
	Log::info("[libretro] plugged in the %s", mouse_active ? "mouse" : "controller");
}

//Scale a raw frontend delta, keeping the fraction that does not survive the cast.
//Without the carry, any sensitivity below 1.0 would throw slow movement away
//entirely - a delta of 1 scaled by 0.5 truncates to 0, so the cursor would sit
//still no matter how long you pushed the mouse gently.
static int scale_mouse_delta(int raw, float scale, float& carry)
{
	if (!raw && carry == 0.0f)
	{
		return 0;
	}
	float scaled = (float)raw * scale + carry;
	int whole = (int)scaled;
	carry = scaled - (float)whole;
	return whole;
}

//Converts a stick axis into the same raw-delta units a real mouse's dx/dy would
//use, so it can go straight into the mouse path and its scaling.
static int analog_axis_to_raw_mouse_delta(int16_t axis)
{
	int magnitude = axis < 0 ? -(int)axis : (int)axis;
	if (magnitude <= ANALOG_MOUSE_DEADZONE)
	{
		return 0;
	}
	float normalized = (float)(magnitude - ANALOG_MOUSE_DEADZONE) / (float)(32767 - ANALOG_MOUSE_DEADZONE);
	int scaled = (int)(normalized * (float)ANALOG_MOUSE_MAX_SPEED);
	return axis < 0 ? -scaled : scaled;
}

//One virtual mouse axis: both sticks plus a held d-pad direction at full speed,
//capped so combining them is no faster than any one alone
static int virtual_mouse_axis(unsigned stick_id, unsigned pad_negative, unsigned pad_positive)
{
	int16_t left = (int16_t)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, stick_id);
	int16_t right = (int16_t)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, stick_id);
	int delta = analog_axis_to_raw_mouse_delta(left) + analog_axis_to_raw_mouse_delta(right);
	if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, pad_negative))
	{
		delta -= ANALOG_MOUSE_MAX_SPEED;
	}
	if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, pad_positive))
	{
		delta += ANALOG_MOUSE_MAX_SPEED;
	}
	return std::clamp(delta, -ANALOG_MOUSE_MAX_SPEED, ANALOG_MOUSE_MAX_SPEED);
}

static void poll_input()
{
	input_poll_cb();

	//Only the device in the port is polled, so no stale input builds up
	if (mouse_active)
	{
		int dx, dy;
		bool mouse_l, mouse_r;
		float scale;
		if (opt_input_device == INPUT_VIRTUAL_MOUSE)
		{
			dx = virtual_mouse_axis(RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT);
			dy = virtual_mouse_axis(RETRO_DEVICE_ID_ANALOG_Y, RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN);
			//RetroPad B is the south face button and A the east one
			mouse_l = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) ||
					  input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L) ||
					  input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
			mouse_r = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) ||
					  input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R) ||
					  input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
			scale = VIRTUAL_MOUSE_BASE_SCALE * opt_mouse_multiplier;
		}
		else
		{
			dx = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
			dy = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
			mouse_l = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) != 0;
			mouse_r = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) != 0;
			scale = REAL_MOUSE_BASE_SCALE * opt_mouse_multiplier;
		}

		int sx = scale_mouse_delta(dx, scale, mouse_carry_x);
		int sy = scale_mouse_delta(dy, scale, mouse_carry_y);
		if (sx || sy)
		{
			//The Loopy mouse Y axis is inverted relative to screen coordinates
			LoopyIO::update_mouse_position(sx, -sy);
		}
		LoopyIO::update_mouse_buttons(Input::MOUSE_L, mouse_l);
		LoopyIO::update_mouse_buttons(Input::MOUSE_R, mouse_r);
		return;
	}

	for (size_t i = 0; i < PAD_MAPPING_COUNT; i++)
	{
		bool pressed = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, PAD_MAPPINGS[i].retro_id) != 0;
		LoopyIO::update_pad(PAD_MAPPINGS[i].loopy_button, pressed);
	}
}

/* ---- Core options ------------------------------------------------------ */

static bool get_option_bool(const char* key, const char* true_value, bool default_value)
{
	retro_variable var = {key, NULL};
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
	{
		return strcmp(var.value, true_value) == 0;
	}
	return default_value;
}

static void RETRO_CALLCONV audio_buffer_status_cb(bool active, unsigned occupancy, bool underrun_likely)
{
	audio_buffer_status_active = active;
	audio_underrun_likely = active && underrun_likely;
}

//'auto' frameskip only drops a frame when the frontend says its audio buffer is
//about to run dry, so it costs nothing while the core keeps up. Registering the
//callback also asks for a little extra audio latency, giving the buffer enough
//slack to absorb the frames we do drop.
static void update_frameskip_setting()
{
	retro_variable var = {"loopy_frameskip", NULL};
	const char* value = "disabled";
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
	{
		value = var.value;
	}

	unsigned prev_max = opt_frameskip_max;
	bool prev_auto = opt_frameskip_auto;

	if (!strcmp(value, "auto"))
	{
		opt_frameskip_auto = true;
		opt_frameskip_max = 3;
	}
	else
	{
		opt_frameskip_auto = false;
		//"disabled", or a fixed number of frames skipped per rendered frame
		opt_frameskip_max = (unsigned)atoi(value);
	}

	if (opt_frameskip_max == prev_max && opt_frameskip_auto == prev_auto)
	{
		return;
	}

	frames_skipped_in_a_row = 0;

	if (opt_frameskip_auto)
	{
		retro_audio_buffer_status_callback cb = {audio_buffer_status_cb};
		if (!environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK, &cb))
		{
			Log::warn("[libretro] frontend has no audio buffer status support; auto frameskip disabled");
			opt_frameskip_auto = false;
			opt_frameskip_max = 0;
			audio_buffer_status_active = false;
		}
		else
		{
			//Roughly one extra frame of audio per frame we may drop
			unsigned latency_ms = 32 + (16 * opt_frameskip_max);
			environ_cb(RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY, &latency_ms);
		}
	}
	else
	{
		//Stop the frontend reporting buffer status, and drop the extra latency
		environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK, NULL);
		unsigned latency_ms = 0;
		environ_cb(RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY, &latency_ms);
		audio_buffer_status_active = false;
		audio_underrun_likely = false;
	}
}

//Options that require a content restart are only read here; the rest are
//also refreshed in retro_run when the frontend signals an update
//Seals go to the frontend's saves directory (the system directory if it has none);
//an empty directory switches the printer off. Applied live.
static void apply_printer_option()
{
	std::string dir;
	if (opt_printer)
	{
		dir = !save_dir.empty() ? save_dir : system_dir;
	}
	config.emulator.image_save_directory = dir;
	if (game_loaded)
	{
		Printer::set_output_directory(dir);
	}
}

static void check_variables(bool startup)
{
	opt_crop_overscan = get_option_bool("loopy_crop_overscan", "enabled", true);

	//Anything else is the controller: every game accepts a pad, while eight of the
	//thirteen never read the mouse
	InputDevice previous_device = opt_input_device;
	opt_input_device = INPUT_CONTROLLER;
	retro_variable device_var = {"loopy_input_device", NULL};
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &device_var) && device_var.value)
	{
		if (!strcmp(device_var.value, "mouse"))
		{
			opt_input_device = INPUT_MOUSE;
		}
		else if (!strcmp(device_var.value, "virtual_mouse"))
		{
			opt_input_device = INPUT_VIRTUAL_MOUSE;
		}
	}

	//Swapped immediately; most games only see it after a restart
	plug_device(opt_input_device != INPUT_CONTROLLER);

	//The remap screen must be told when the port changes hands
	if (opt_input_device != previous_device)
	{
		set_input_descriptors();
	}

	//Applied live - the format only affects how the next print is encoded
	opt_seal_format = ImageWriter::IMAGE_TYPE_PNG;
	retro_variable seal_var = {"loopy_seal_format", NULL};
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &seal_var) && seal_var.value)
	{
		if (!strcmp(seal_var.value, "bmp"))
		{
			opt_seal_format = ImageWriter::IMAGE_TYPE_BMP;
		}
	}
	Printer::set_image_type(opt_seal_format);

	retro_variable sens_var = {"loopy_mouse_sensitivity", NULL};
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &sens_var) && sens_var.value)
	{
		float sensitivity = (float)atof(sens_var.value);
		if (sensitivity > 0.0f)
		{
			opt_mouse_multiplier = sensitivity;
		}
	}

	//Synth headroom; applied live (no-op until the sound engine exists)
	retro_variable var = {"loopy_mix_level", NULL};
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
	{
		Sound::set_mix_level((float)atof(var.value));
	}

	//Skipping provably-idle vblank spin loops; the games burn most of their CPU
	//time in them, so this is a large speedup on weak hardware
	SH2::set_idle_skip(get_option_bool("loopy_idle_skip", "enabled", true));

	update_frameskip_setting();

	bool printer = get_option_bool("loopy_printer", "enabled", true);
	if (startup || printer != opt_printer)
	{
		opt_printer = printer;
		apply_printer_option();
	}

	if (startup)
	{
		opt_debug_log = get_option_bool("loopy_debug_log", "enabled", false);

		//Sets the scanline length and the audio rate, so it has to be known before
		//System::initialize and stays fixed until content is restarted
		Timing::set_hardware_refresh(get_option_bool("loopy_refresh_rate", "hardware", true));
	}
}

//Re-asserts the option's device after System::initialize (which plugs in the pad),
//before the first frame: games choose their device at boot
static void apply_controller_choice()
{
	mouse_active = false;
	LoopyIO::set_controller_plugged(true, false);

	if (opt_input_device != INPUT_CONTROLLER)
	{
		plug_device(true);
	}
}

/* ---- File helpers ------------------------------------------------------ */

static bool read_file(const std::string& path, std::vector<uint8_t>& out)
{
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open())
	{
		return false;
	}
	out.assign(std::istreambuf_iterator<char>(file), {});
	return true;
}

/* ---- libretro API ------------------------------------------------------ */

RETRO_API void retro_set_environment(retro_environment_t cb)
{
	environ_cb = cb;

	libretro_set_core_options(cb);

	retro_log_callback log_interface;
	if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_interface))
	{
		log_cb = log_interface.log;
		Log::set_sink(log_sink);
	}

	static const struct retro_controller_description port0_devices[] = {
		{"Loopy Gamepad", RETRO_DEVICE_JOYPAD},
	};
	static const struct retro_controller_info ports[] = {
		{port0_devices, 1},
		{NULL, 0},
	};
	cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
	video_cb = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
	(void)cb;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
	audio_batch_cb = cb;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb)
{
	input_poll_cb = cb;
}

RETRO_API void retro_set_input_state(retro_input_state_t cb)
{
	input_state_cb = cb;
}

RETRO_API void retro_init(void)
{
	//LOOPY_DEBUG in the environment enables verbose core logging
	Log::set_level(getenv("LOOPY_DEBUG") ? Log::VERBOSE : Log::INFO);

	const char* dir = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
	{
		system_dir = dir;
	}
	dir = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir)
	{
		save_dir = dir;
	}
}

RETRO_API void retro_deinit(void)
{
	log_cb = NULL;
	Log::set_sink(NULL);
}

RETRO_API unsigned retro_api_version(void)
{
	return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(struct retro_system_info* info)
{
	memset(info, 0, sizeof(*info));
	info->library_name = CORE_NAME;
	info->library_version = CORE_VERSION;
	info->valid_extensions = "bin|loopy";
	info->need_fullpath = false;
	info->block_extract = false;
}

//Width of the frame last delivered: DISPLAY_WIDTH normally, HIRES_DISPLAY_WIDTH
//for a frame containing hi-res scanlines (see render_video_frame)
static unsigned current_display_width = Video::DISPLAY_WIDTH;

RETRO_API void retro_get_system_av_info(struct retro_system_av_info* info)
{
	memset(info, 0, sizeof(*info));
	info->geometry.base_width = current_display_width;
	info->geometry.base_height = current_display_height;
	//Room for hi-res frames, which are delivered twice as wide
	info->geometry.max_width = Video::HIRES_DISPLAY_WIDTH;
	info->geometry.max_height = Video::DISPLAY_HEIGHT;
	info->geometry.aspect_ratio = 4.0f / 3.0f;
	//60 Hz or the hardware's 59.8261 Hz (Video > Refresh Rate), with the audio rate
	//that gives each frame a whole 800 samples
	info->timing.fps = Timing::frame_rate();
	info->timing.sample_rate = Sound::output_sample_rate();
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
	(void)port;
	(void)device;
}

RETRO_API void retro_reset(void)
{
	if (!game_loaded)
	{
		return;
	}

	//Pick up any option changes made since the last load/reset
	check_variables(false);

	//Cart::shutdown (via System::shutdown) copies live SRAM back into the
	//config so it survives the reinitialization
	System::shutdown(config);
	System::initialize(config);
	apply_controller_choice();

	//The sound engine was just recreated; re-apply live audio options
	check_variables(false);
}

static unsigned target_display_height()
{
	if (!opt_crop_overscan)
	{
		return Video::DISPLAY_HEIGHT;
	}
	return (unsigned)Video::get_display_scanlines();
}

//Tells the frontend when the picture's size changes: 224 or 240 lines as the game
//sets, and 256 or 512 wide per frame. The aspect ratio never changes - a hi-res
//frame covers the same area of the screen, with twice the columns.
static void update_geometry_if_needed(unsigned width)
{
	unsigned height = target_display_height();
	if (height == current_display_height && width == current_display_width)
	{
		return;
	}
	current_display_height = height;
	current_display_width = width;

	retro_game_geometry geometry;
	geometry.base_width = current_display_width;
	geometry.base_height = current_display_height;
	geometry.max_width = Video::HIRES_DISPLAY_WIDTH;
	geometry.max_height = Video::DISPLAY_HEIGHT;
	geometry.aspect_ratio = 4.0f / 3.0f;
	environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
}

static inline uint16_t rgb555_to_rgb565(uint16_t c)
{
	uint16_t r = (c >> 10) & 0x1F;
	uint16_t g = (c >> 5) & 0x1F;
	uint16_t b = c & 0x1F;
	//Expand green from 5 to 6 bits, replicating the top bit
	return (uint16_t)((r << 11) | (g << 6) | ((g >> 4) << 5) | b);
}

//The display buffer is already RGB565, so the frame is handed over as-is; only rows
//outside the picture (overscan cropping off) are filled with the backdrop. A frame
//with a hi-res scanline is assembled and delivered 512 wide.
static void render_video_frame()
{
	unsigned drawn = (unsigned)Video::get_display_scanlines();

	if (Video::frame_has_hires())
	{
		update_geometry_if_needed(Video::HIRES_DISPLAY_WIDTH);
		unsigned hires_height = current_display_height;
		unsigned top = hires_height > drawn ? (hires_height - drawn) / 2 : 0;
		uint16_t backdrop = rgb555_to_rgb565(Video::get_background_color());
		uint16_t* frame = Video::compose_hires_frame((int)drawn, (int)hires_height, backdrop, (int)top);
		video_cb(frame, Video::HIRES_DISPLAY_WIDTH, hires_height,
				 Video::HIRES_DISPLAY_WIDTH * sizeof(uint16_t));
		return;
	}

	uint16_t* display = System::get_display_output();

	update_geometry_if_needed(Video::DISPLAY_WIDTH);

	unsigned height = current_display_height;

	//With cropping off the frame is always 240 lines, a 224-line picture centred in
	//it (240-line mode adds eight lines above and below). Rows are rewritten every
	//frame, so moving them down in place is safe.
	if (height > drawn)
	{
		const size_t width = Video::DISPLAY_WIDTH;
		unsigned top = (height - drawn) / 2;
		uint16_t backdrop = rgb555_to_rgb565(Video::get_background_color());
		memmove(display + top * width, display, drawn * width * sizeof(uint16_t));
		std::fill_n(display, top * width, backdrop);
		std::fill_n(display + (top + drawn) * width, (height - top - drawn) * width, backdrop);
	}

	video_cb(display, Video::DISPLAY_WIDTH, height, Video::DISPLAY_WIDTH * sizeof(uint16_t));
}

/* ---- Debug Log File ---------------------------------------------------- */

//Directory for the session log and crash dumps: the frontend's saves directory, as for
//printed seals, or the system directory when there is none.
static std::string debug_output_dir()
{
	return !save_dir.empty() ? save_dir : system_dir;
}

static std::string content_stem()
{
	size_t slash = content_path.find_last_of("/\\");
	std::string name = slash == std::string::npos ? content_path : content_path.substr(slash + 1);
	size_t dot = name.find_last_of('.');
	if (dot != std::string::npos)
	{
		name.resize(dot);
	}
	return name.empty() ? std::string("content") : name;
}

static std::string timestamp_now()
{
	char stamp[32];
	std::time_t now = std::time(nullptr);
	std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&now));
	return stamp;
}

static void session_log_open(const struct retro_game_info* game)
{
	std::string path = debug_output_dir() + "/" + content_stem() + "_" + timestamp_now() + ".log";
	session_log = fopen(path.c_str(), "w");
	if (!session_log)
	{
		Log::warn("[libretro] could not open debug log %s", path.c_str());
		return;
	}
	const uint8_t* rom = (const uint8_t*)game->data;
	uint32_t checksum = ((uint32_t)rom[8] << 24) | ((uint32_t)rom[9] << 16) | ((uint32_t)rom[10] << 8) | rom[11];
	fprintf(session_log, "gloopy %s debug log - %s, %u bytes, header checksum %08X, %.4f Hz\n", CORE_VERSION,
			content_stem().c_str(), (unsigned)game->size, checksum, Timing::frame_rate());
	for (const retro_core_option_v2_definition* def = option_defs_us; def->key; def++)
	{
		struct retro_variable var = {def->key, NULL};
		const char* value = environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value ? var.value : def->default_value;
		fprintf(session_log, "option %s = %s\n", def->key, value ? value : "?");
	}
	fprintf(session_log, "[   frame  mm:ss.t]\n");
	fflush(session_log);
	Log::info("[libretro] debug log: %s", path.c_str());
}

static void session_log_close()
{
	if (!session_log)
	{
		return;
	}
	session_log_line("INFO", "[libretro] session end");
	fclose(session_log);
	session_log = nullptr;
}

//First invalid instruction of the session (the CPU ran into data): keep the evidence.
static void dump_crash_state()
{
	const std::string base = debug_output_dir() + "/" + content_stem() + "_crash_" + timestamp_now();
	std::vector<uint8_t> state(retro_serialize_size());
	bool ok_state = !state.empty() && retro_serialize(state.data(), state.size());
	if (ok_state)
	{
		std::ofstream(base + ".state", std::ios::binary).write((const char*)state.data(), state.size());
	}
	const void* wram = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
	size_t wram_size = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
	if (wram && wram_size)
	{
		std::ofstream(base + "_wram.bin", std::ios::binary).write((const char*)wram, wram_size);
	}
	Log::error("[libretro] CPU ran into invalid code at %08X; saved %s.state%s and %s_wram.bin",
			   SH2::Interpreter::unrecognized_first_pc, base.c_str(), ok_state ? "" : " (failed)", base.c_str());
}

RETRO_API void retro_run(void)
{
	bool updated = false;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
	{
		check_variables(false);
	}

	poll_input();

	//Decide whether to drop this frame's rendering. The emulated machine still
	//runs in full - only VDP compositing and the blit are skipped - so game
	//logic, timing and audio are identical either way.
	bool skip_frame = false;
	if (opt_frameskip_max && frames_skipped_in_a_row < opt_frameskip_max)
	{
		if (opt_frameskip_auto)
		{
			skip_frame = audio_buffer_status_active && audio_underrun_likely;
		}
		else
		{
			//Fixed ratio: render one frame, then skip up to opt_frameskip_max
			skip_frame = (frame_counter % (opt_frameskip_max + 1)) != 0;
		}
	}
	frame_counter++;

	frames_skipped_in_a_row = skip_frame ? frames_skipped_in_a_row + 1 : 0;

	Video::set_render_enabled(!skip_frame);

	System::run();

	if (skip_frame)
	{
		//A NULL frame tells the frontend to repeat the previous one
		video_cb(NULL, current_display_width, current_display_height,
				 current_display_width * sizeof(uint16_t));
	}
	else
	{
		render_video_frame();
	}

	Sound::render(audio_buffer, AUDIO_FRAMES_PER_VIDEO_FRAME);
	audio_batch_cb(audio_buffer, AUDIO_FRAMES_PER_VIDEO_FRAME);

	if (session_log && !crash_dumped && SH2::Interpreter::unrecognized_count)
	{
		crash_dumped = true;
		dump_crash_state();
	}
}

RETRO_API size_t retro_serialize_size(void)
{
	if (!game_loaded)
	{
		return 0;
	}

	if (!serialize_size_cache)
	{
		//The state size varies slightly with the number of pending scheduler
		//events, so measure once and add generous fixed headroom. The frontend
		//requires this value to stay constant while the content runs.
		SaveState::Snapshot ss;
		System::save_state(ss);
		serialize_size_cache = ss.size() + 0x10000;
	}
	return serialize_size_cache;
}

RETRO_API bool retro_serialize(void* data, size_t size)
{
	if (!game_loaded || size < retro_serialize_size())
	{
		return false;
	}

	SaveState::Snapshot ss;
	System::save_state(ss);
	if (ss.size() > size)
	{
		Log::error("[libretro] save state exceeds reported size (%u > %u)", (unsigned)ss.size(), (unsigned)size);
		return false;
	}

	memcpy(data, ss.data(), ss.size());
	memset((uint8_t*)data + ss.size(), 0, size - ss.size());
	return true;
}

RETRO_API bool retro_unserialize(const void* data, size_t size)
{
	if (!game_loaded)
	{
		return false;
	}

	SaveState::Snapshot ss;
	ss.assign(data, size);
	return System::load_state(ss);
}

RETRO_API void retro_cheat_reset(void)
{
}

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char* code)
{
	(void)index;
	(void)enabled;
	(void)code;
}

//Firmware lives flat in the system directory, but a loopy/ subdirectory is
//accepted as well since users commonly organize BIOS files into subfolders,
//and the directory containing the loaded ROM is searched last (the standalone
//emulator does the same)
//
//A candidate of the wrong size is rejected and the search continues, so a stray
//file cannot shadow a good copy further down the list. Both firmware files have
//exactly one correct size, and the wrong ones fail badly but quietly.
static bool find_firmware(const char* filename, size_t expected_size, std::vector<uint8_t>& out,
						  std::string& found_path)
{
	std::vector<std::string> candidates = {
		system_dir + "/" + filename,
		system_dir + "/loopy/" + filename,
	};

	size_t dir_end = content_path.find_last_of("/\\");
	if (dir_end != std::string::npos)
	{
		std::string content_dir = content_path.substr(0, dir_end);
		candidates.push_back(content_dir + "/" + filename);
		candidates.push_back(content_dir + "/loopy/" + filename);
	}

	for (const auto& path : candidates)
	{
		if (!read_file(path, out))
			continue;

		if (out.size() != expected_size)
		{
			Log::warn("[libretro] ignoring %s: it is %u bytes, expected %u", path.c_str(), (unsigned)out.size(),
					  (unsigned)expected_size);
			out.clear();
			continue;
		}

		found_path = path;
		return true;
	}
	return false;
}

static bool load_bios_files()
{
	//An empty system_dir means the frontend served no system directory, and the
	//paths above degrade to the filesystem root; say so rather than printing ()
	const char* where = system_dir.empty() ? "(the frontend reported no system directory)" : system_dir.c_str();

	std::string bios_path;
	if (!find_firmware("loopy_bios.bin", Memory::BIOS_SIZE, config.bios_rom, bios_path))
	{
		Log::error("[libretro] missing required BIOS: place loopy_bios.bin (%u bytes) in the frontend system "
				   "directory %s",
				   (unsigned)Memory::BIOS_SIZE, where);
		return false;
	}
	Log::info("[libretro] loaded BIOS from %s", bios_path.c_str());

	std::string sound_bios_path;
	if (find_firmware("loopy_soundbios.bin", Sound::SOUND_ROM_SIZE, config.sound_rom, sound_bios_path))
	{
		Log::info("[libretro] loaded sound BIOS from %s", sound_bios_path.c_str());
	}
	else
	{
		//Degrade the same way a missing sound BIOS already does: silent, not broken
		Log::warn("[libretro] no usable loopy_soundbios.bin (%u bytes) found in %s; emulation continues without sound",
				  (unsigned)Sound::SOUND_ROM_SIZE, where);
	}
	return true;
}

RETRO_API bool retro_load_game(const struct retro_game_info* game)
{
	if (!game || !game->data || game->size < 0x18)
	{
		Log::error("[libretro] invalid or empty ROM");
		return false;
	}

	enum retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_RGB565;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixel_format))
	{
		Log::error("[libretro] frontend does not support RGB565");
		return false;
	}

	config = {};
	serialize_size_cache = 0;
	content_path = game->path ? game->path : "";

	if (!load_bios_files())
	{
		return false;
	}

	const uint8_t* rom = (const uint8_t*)game->data;
	config.cart.rom.assign(rom, rom + game->size);

	//The expansion module locates Wanwan's PCM samples relative to the ROM path.
	//Nothing else reads this. The expansion chip is offered to every cart but only
	//Wanwan ever drives it, so a game that does not write to it never hears from
	//it, and this costs the rest nothing..
	config.cart.rom_path = content_path;

	//SRAM size comes from the cartridge header (big-endian start/end addresses).
	//Some dumps have degenerate headers (Magical Shop has no header at all);
	//tolerate them with no SRAM rather than refusing to load, matching the
	//standalone emulator.
	uint32_t sram_start, sram_end;
	memcpy(&sram_start, config.cart.rom.data() + 0x10, 4);
	memcpy(&sram_end, config.cart.rom.data() + 0x14, 4);
	uint32_t sram_size = Common::bswp32(sram_end) - Common::bswp32(sram_start) + 1;
	if (sram_size > 0x100000)
	{
		Log::warn("[libretro] implausible SRAM size in cartridge header (0x%08X); continuing without SRAM", sram_size);
		sram_size = 0;
	}
	config.cart.sram.assign(sram_size, 0xFF);
	//SRAM content persistence is handled by the frontend via retro_get_memory

	//Also sets the printer's directory (apply_printer_option)
	check_variables(true);

	//After check_variables, not before: Printer::initialize takes the format from
	//config, so seeding it from a value the options had not been read into yet
	//would quietly discard the player's choice on the very first load
	config.emulator.printer_image_type = opt_seal_format;

	System::initialize(config);
	game_loaded = true;

	frame_counter = 0;
	SH2::Interpreter::unrecognized_count = 0;
	SH2::Bus::reset_unmapped_reports();
	crash_dumped = false;
	if (opt_debug_log)
	{
		session_log_open(game);
	}
	Video::set_snow_reports(opt_debug_log);

	apply_controller_choice();

	//Apply live options that need the initialized system (e.g. synth mix level)
	check_variables(false);

	set_input_descriptors();

	current_display_height = 0;
	update_geometry_if_needed(Video::DISPLAY_WIDTH);

	return true;
}

RETRO_API bool retro_load_game_special(unsigned game_type, const struct retro_game_info* info, size_t num_info)
{
	(void)game_type;
	(void)info;
	(void)num_info;
	return false;
}

RETRO_API void retro_unload_game(void)
{
	session_log_close();
	if (game_loaded)
	{
		System::shutdown(config);
		game_loaded = false;
	}
	config = {};
	content_path.clear();
	serialize_size_cache = 0;
}

RETRO_API unsigned retro_get_region(void)
{
	//The Loopy was a Japan-only NTSC system
	return RETRO_REGION_NTSC;
}

RETRO_API void* retro_get_memory_data(unsigned id)
{
	if (!game_loaded)
	{
		return NULL;
	}

	switch (id)
	{
	case RETRO_MEMORY_SAVE_RAM:
		return Cart::get_sram_ptr();
	case RETRO_MEMORY_SYSTEM_RAM:
		//Work RAM is mapped contiguously in the SH-2 page table
		return Memory::get_sh2_pagetable()[Memory::RAM_START / 0x1000];
	default:
		return NULL;
	}
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
	if (!game_loaded)
	{
		return 0;
	}

	switch (id)
	{
	case RETRO_MEMORY_SAVE_RAM:
		return Cart::get_sram_size();
	case RETRO_MEMORY_SYSTEM_RAM:
		return Memory::RAM_SIZE;
	default:
		return 0;
	}
}
