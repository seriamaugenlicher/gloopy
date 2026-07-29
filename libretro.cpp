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
#define CORE_VERSION "1.0.1" GIT_VERSION

static constexpr int FPS = 60;
static constexpr int SAMPLE_RATE = Sound::TARGET_SAMPLE_RATE;
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

//Which device the port is allowed to hold. AUTO plugs in whichever the player
//actually uses; the Loopy has one controller port, so never both at once.
enum InputDevice
{
	INPUT_AUTO,
	INPUT_CONTROLLER,
	INPUT_MOUSE,
};
static InputDevice opt_input_device = INPUT_AUTO;

//Scales the frontend's raw mouse movement. libretro has no sensitivity control -
//it reports relative movement and leaves the interpretation to the core.
//
//What the option calls 1x is three eighths of the frontend's raw movement: the
//Loopy's own mouse was a low-resolution ball mouse, and passing a modern optical
//mouse's deltas through untouched sends the cursor flying. 
//
//Calibrating 1x to what actually feels right keeps the multipliers honest to the
//player: 2x really is twice the default speed.

constexpr static float MOUSE_BASE_SCALE = 0.375f;
static float opt_mouse_sensitivity = MOUSE_BASE_SCALE;

//Frameskip. The emulated CPU always runs; only VDP compositing and the frame
//blit are dropped, so game logic and audio are untouched. 0 = off, otherwise
//the maximum number of frames that may be skipped in a row.
static unsigned opt_frameskip_max = 0;
static bool opt_frameskip_auto = false;
static unsigned frames_skipped_in_a_row = 0;
static unsigned frame_counter = 0;

//Fed by the frontend when frameskip 'auto' is active
static bool audio_underrun_likely = false;
static bool audio_buffer_status_active = false;

//Which device is plugged into the port right now.
//
//Swapping mid-game is safe and needs no reset: a game selects what the hardware
//scans by writing pad_scan/mouse_scan bits in a VDP mode register, and it picks
//those by reading the port - a pad and a mouse return different signatures. The
//games poll that continuously, so they re-detect a swap on their own, exactly as
//they would if you unplugged one and plugged in the other.
static bool mouse_active = false;

//Real content path, kept separately so the expansion-PCM option can decide
//whether the core sees it
static std::string content_path;

static size_t serialize_size_cache;

/* ---- Logging ---------------------------------------------------------- */

static void log_sink(Log::Level level, const char* message)
{
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
	{RETRO_DEVICE_ID_JOYPAD_A, Input::PAD_A},
	{RETRO_DEVICE_ID_JOYPAD_B, Input::PAD_B},
	{RETRO_DEVICE_ID_JOYPAD_X, Input::PAD_C},
	{RETRO_DEVICE_ID_JOYPAD_Y, Input::PAD_D},
	{RETRO_DEVICE_ID_JOYPAD_L, Input::PAD_L1},
	{RETRO_DEVICE_ID_JOYPAD_R, Input::PAD_R1},
	{RETRO_DEVICE_ID_JOYPAD_START, Input::PAD_START},
};

constexpr static size_t PAD_MAPPING_COUNT = sizeof(PAD_MAPPINGS) / sizeof(PAD_MAPPINGS[0]);

static void set_input_descriptors()
{
	static const struct retro_input_descriptor descriptors[] = {
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "D-Pad Up"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "D-Pad Down"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "D-Pad Left"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "D"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "C"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start"},
		{0, 0, 0, 0, NULL},
	};

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
static int scale_mouse_delta(int raw, float& carry)
{
	if (!raw && carry == 0.0f)
	{
		return 0;
	}
	float scaled = (float)raw * opt_mouse_sensitivity + carry;
	int whole = (int)scaled;
	carry = scaled - (float)whole;
	return whole;
}

static void poll_input()
{
	input_poll_cb();

	int dx = 0, dy = 0;
	bool mouse_l = false, mouse_r = false;
	if (opt_input_device != INPUT_CONTROLLER)
	{
		dx = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
		dy = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
		mouse_l = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) != 0;
		mouse_r = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) != 0;
	}

	bool pad_pressed[PAD_MAPPING_COUNT] = {};
	bool any_pad_pressed = false;
	if (opt_input_device != INPUT_MOUSE)
	{
		for (size_t i = 0; i < PAD_MAPPING_COUNT; i++)
		{
			pad_pressed[i] = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, PAD_MAPPINGS[i].retro_id) != 0;
			any_pad_pressed |= pad_pressed[i];
		}
	}

	//Swap to whichever device the player just used. A mouse only reports a delta
	//when it is actually moved, so a still mouse cannot steal the port from a pad
	//being played, and a machine with no mouse at all never reports one.
	if (opt_input_device == INPUT_AUTO)
	{
		if (!mouse_active && (dx || dy || mouse_l || mouse_r))
		{
			plug_device(true);
		}
		else if (mouse_active && any_pad_pressed)
		{
			plug_device(false);
		}
	}

	//Only the plugged device is fed; the other one's state is ignored by the IO
	//registers anyway, and letting it accumulate would surface stale input on the
	//next swap
	if (mouse_active)
	{
		int sx = scale_mouse_delta(dx, mouse_carry_x);
		int sy = scale_mouse_delta(dy, mouse_carry_y);
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
		LoopyIO::update_pad(PAD_MAPPINGS[i].loopy_button, pad_pressed[i]);
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
static void check_variables(bool startup)
{
	opt_crop_overscan = get_option_bool("loopy_crop_overscan", "enabled", true);

	//Applied live: swapping the device mid-game needs no restart (see mouse_active)
	opt_input_device = INPUT_AUTO;
	retro_variable device_var = {"loopy_input_device", NULL};
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &device_var) && device_var.value)
	{
		if (!strcmp(device_var.value, "controller"))
		{
			opt_input_device = INPUT_CONTROLLER;
		}
		else if (!strcmp(device_var.value, "mouse"))
		{
			opt_input_device = INPUT_MOUSE;
		}
	}

	//A fixed choice is plugged in immediately. AUTO leaves whatever is already
	//there and lets poll_input swap on first use, so that merely opening the menu
	//does not yank the device out from under a game mid-play.
	if (opt_input_device == INPUT_CONTROLLER)
	{
		plug_device(false);
	}
	else if (opt_input_device == INPUT_MOUSE)
	{
		plug_device(true);
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
			opt_mouse_sensitivity = sensitivity * MOUSE_BASE_SCALE;
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

#ifdef LOOPY_PC_PROFILE
	SH2::pc_profile_enable(get_option_bool("loopy_profile_pc", "enabled", false));
#endif

	if (startup)
	{
		opt_printer = get_option_bool("loopy_printer", "enabled", true);
	}
}

//Called after System::initialize, which plugs the pad in itself (Input::initialize),
//so re-assert the option's choice on top of it. AUTO starts on the controller and
//swaps on first use
static void apply_controller_choice()
{
	mouse_active = false;
	LoopyIO::set_controller_plugged(true, false);

	if (opt_input_device == INPUT_MOUSE)
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

RETRO_API void retro_get_system_av_info(struct retro_system_av_info* info)
{
	memset(info, 0, sizeof(*info));
	info->geometry.base_width = Video::DISPLAY_WIDTH;
	info->geometry.base_height = current_display_height;
	info->geometry.max_width = Video::DISPLAY_WIDTH;
	info->geometry.max_height = Video::DISPLAY_HEIGHT;
	info->geometry.aspect_ratio = 4.0f / 3.0f;
	info->timing.fps = (double)FPS;
	info->timing.sample_rate = (double)SAMPLE_RATE;
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

static void update_geometry_if_needed()
{
	unsigned height = target_display_height();
	if (height == current_display_height)
	{
		return;
	}
	current_display_height = height;

	retro_game_geometry geometry;
	geometry.base_width = Video::DISPLAY_WIDTH;
	geometry.base_height = current_display_height;
	geometry.max_width = Video::DISPLAY_WIDTH;
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

//The renderer already writes RGB565 straight into the display buffer (see
//display_encode in render.cpp), so the frame is handed to the frontend as-is -
//no conversion pass, no staging copy. Only rows the VDP never composited need
//filling: with overscan cropping off, the frame is taller than the game's
//visible area, and those extra rows show the backdrop.
static void render_video_frame()
{
	uint16_t* display = System::get_display_output();

	update_geometry_if_needed();

	unsigned height = current_display_height;
	unsigned drawn = (unsigned)Video::get_display_scanlines();

	if (height > drawn)
	{
		uint16_t backdrop = rgb555_to_rgb565(Video::get_background_color());
		uint16_t* row = display + (size_t)drawn * Video::DISPLAY_WIDTH;
		for (size_t i = 0; i < (size_t)(height - drawn) * Video::DISPLAY_WIDTH; i++)
		{
			row[i] = backdrop;
		}
	}

	video_cb(display, Video::DISPLAY_WIDTH, height, Video::DISPLAY_WIDTH * sizeof(uint16_t));
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
		video_cb(NULL, Video::DISPLAY_WIDTH, current_display_height,
				 Video::DISPLAY_WIDTH * sizeof(uint16_t));
	}
	else
	{
		render_video_frame();
	}

	Sound::render(audio_buffer, AUDIO_FRAMES_PER_VIDEO_FRAME);
	audio_batch_cb(audio_buffer, AUDIO_FRAMES_PER_VIDEO_FRAME);
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
	config.cart.sram_file_path = "";

	//The printer saves seal images into the frontend save directory
	if (opt_printer)
	{
		std::string print_dir = !save_dir.empty() ? save_dir : system_dir;
		config.emulator.image_save_directory = print_dir;
	}
	config.emulator.printer_correct_aspect_ratio = false;
	config.emulator.printer_view_command = "";

	check_variables(true);
	if (!opt_printer)
	{
		config.emulator.image_save_directory.clear();
	}

	//After check_variables, not before: Printer::initialize takes the format from
	//config, so seeding it from a value the options had not been read into yet
	//would quietly discard the player's choice on the very first load
	config.emulator.printer_image_type = opt_seal_format;

	System::initialize(config);
	game_loaded = true;

	apply_controller_choice();

	//Apply live options that need the initialized system (e.g. synth mix level)
	check_variables(false);

	set_input_descriptors();

	current_display_height = 0;
	update_geometry_if_needed();

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
	if (game_loaded)
	{
#ifdef LOOPY_PC_PROFILE
		SH2::pc_profile_report();
#endif
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
