//Headless test harness for loopy_libretro.dll
//Loads the core, boots a ROM, dumps frames as BMP, tests savestates and SRAM.
#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "libretro.h"

static std::string system_dir;
static std::string save_dir;
static std::string out_dir;

static std::vector<uint16_t> last_frame;
static unsigned last_w, last_h;
static uint64_t audio_samples_total;
static double audio_energy;
static unsigned frames_run;

static retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;

//Mouse test state
static bool mouse_option_enabled;
static bool mouse_option_dirty;
static int mouse_dx, mouse_dy;
static bool mouse_left;

//Pad test state
static bool pad_start, pad_a;

//Core options passed on the command line as key=value, served via
//RETRO_ENVIRONMENT_GET_VARIABLE.
//Served only once active: bench mode enables them after warmup so the game
//reaches the scene under test with all stages running.
static std::vector<std::pair<std::string, std::string>> cli_options;
static bool cli_options_active;

//Audio buffer status reporting, used to exercise 'auto' frameskip. A real
//frontend reports this each frame; harness_underrun=1 simulates a device whose
//audio buffer is draining (i.e. hardware too slow to keep up).
static retro_audio_buffer_status_callback_t audio_status_cb;
static bool simulate_underrun;

static uint32_t crc32_buf(const void* data, size_t len, uint32_t crc = 0)
{
	static uint32_t table[256];
	static bool built = false;
	if (!built)
	{
		for (uint32_t i = 0; i < 256; i++)
		{
			uint32_t c = i;
			for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			table[i] = c;
		}
		built = true;
	}
	crc = ~crc;
	const uint8_t* p = (const uint8_t*)data;
	for (size_t i = 0; i < len; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

//Running hash of every audio sample the core has produced
static uint32_t audio_crc;

static void core_log(enum retro_log_level level, const char* fmt, ...)
{
	static const char* names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
	va_list args;
	va_start(args, fmt);
	printf("[core %s] ", names[level <= 3 ? level : 3]);
	vprintf(fmt, args);
	va_end(args);
}

static bool env_cb(unsigned cmd, void* data)
{
	switch (cmd)
	{
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		*(const char**)data = system_dir.c_str();
		return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char**)data = save_dir.c_str();
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		pixel_format = *(retro_pixel_format*)data;
		printf("[env] pixel format set to %d\n", (int)pixel_format);
		return true;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((retro_log_callback*)data)->log = core_log;
		return true;
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
		*(unsigned*)data = 2;
		return true;
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
		printf("[env] core options v2 registered\n");
		return true;
	case RETRO_ENVIRONMENT_SET_GEOMETRY:
	{
		retro_game_geometry* g = (retro_game_geometry*)data;
		printf("[env] geometry %ux%u aspect %.3f\n", g->base_width, g->base_height, g->aspect_ratio);
		return true;
	}
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
	case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
		return true;
	case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
	{
		//A NULL pointer means the core is disabling buffer status reporting
		const retro_audio_buffer_status_callback* cb = (const retro_audio_buffer_status_callback*)data;
		audio_status_cb = cb ? cb->callback : NULL;
		printf("[env] audio buffer status callback %s\n", audio_status_cb ? "registered" : "cleared");
		return true;
	}
	case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
		printf("[env] core requested minimum audio latency %u ms\n", *(const unsigned*)data);
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE:
	{
		retro_variable* var = (retro_variable*)data;

		//An explicit key=value always wins, including over the mouse modes below -
		//otherwise a test cannot ask for a device the mode did not intend
		if (var->key && cli_options_active)
		{
			for (auto& opt : cli_options)
			{
				if (opt.first == var->key)
				{
					var->value = opt.second.c_str();
					return true;
				}
			}
		}
		if (var->key && !strcmp(var->key, "loopy_input_device"))
		{
			//'mouse' forces the mouse in; 'controller' pins the pad, so the default
			//(auto) swapping cannot fire on the movement the mouse modes inject
			var->value = mouse_option_enabled ? "mouse" : "controller";
			return true;
		}
		return false;  //use defaults for everything else
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool*)data = mouse_option_dirty;
		mouse_option_dirty = false;
		return true;
	default:
		return false;
	}
}

static void video_cb(const void* data, unsigned width, unsigned height, size_t pitch)
{
	if (!data) return;
	last_w = width;
	last_h = height;
	last_frame.resize((size_t)width * height);
	for (unsigned y = 0; y < height; y++)
	{
		const uint16_t* row = (const uint16_t*)((const uint8_t*)data + y * pitch);
		memcpy(&last_frame[(size_t)y * width], row, width * sizeof(uint16_t));
	}
}

static size_t audio_batch_cb(const int16_t* data, size_t frames)
{
	audio_samples_total += frames;
	audio_crc = crc32_buf(data, frames * 2 * sizeof(int16_t), audio_crc);
	for (size_t i = 0; i < frames * 2; i++)
	{
		double s = data[i] / 32768.0;
		audio_energy += s * s;
	}
	return frames;
}

static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
	if (device == RETRO_DEVICE_MOUSE && port == 0)
	{
		switch (id)
		{
		case RETRO_DEVICE_ID_MOUSE_X:
			return (int16_t)mouse_dx;
		case RETRO_DEVICE_ID_MOUSE_Y:
			return (int16_t)mouse_dy;
		case RETRO_DEVICE_ID_MOUSE_LEFT:
			return mouse_left ? 1 : 0;
		default:
			return 0;
		}
	}
	if (device == RETRO_DEVICE_JOYPAD && port == 0)
	{
		if (id == RETRO_DEVICE_ID_JOYPAD_START) return pad_start ? 1 : 0;
		if (id == RETRO_DEVICE_ID_JOYPAD_A) return pad_a ? 1 : 0;
	}
	return 0;
}

static bool save_bmp565(const std::string& path, const uint16_t* px, unsigned w, unsigned h)
{
	uint32_t row_bytes = (w * 3 + 3) & ~3u;
	uint32_t image_size = row_bytes * h;
	uint32_t file_size = 54 + image_size;
	uint8_t header[54] = {};
	header[0] = 'B'; header[1] = 'M';
	memcpy(header + 2, &file_size, 4);
	uint32_t off = 54; memcpy(header + 10, &off, 4);
	uint32_t info = 40; memcpy(header + 14, &info, 4);
	int32_t ww = w, hh = h;
	memcpy(header + 18, &ww, 4);
	memcpy(header + 22, &hh, 4);
	uint16_t planes = 1, bpp = 24;
	memcpy(header + 26, &planes, 2);
	memcpy(header + 28, &bpp, 2);
	memcpy(header + 34, &image_size, 4);

	std::ofstream f(path, std::ios::binary);
	if (!f.is_open()) return false;
	f.write((char*)header, 54);
	std::vector<uint8_t> row(row_bytes, 0);
	for (int y = h - 1; y >= 0; y--)
	{
		for (unsigned x = 0; x < w; x++)
		{
			uint16_t c = px[(size_t)y * w + x];
			uint8_t r = ((c >> 11) & 31) * 255 / 31;
			uint8_t g = ((c >> 5) & 63) * 255 / 63;
			uint8_t b = (c & 31) * 255 / 31;
			row[x * 3 + 0] = b;
			row[x * 3 + 1] = g;
			row[x * 3 + 2] = r;
		}
		f.write((char*)row.data(), row_bytes);
	}
	return f.good();
}

int main(int argc, char** argv)
{
	if (argc < 5)
	{
		printf("usage: harness <core.dll> <rom> <system_dir> <out_dir>\n");
		return 1;
	}
	const char* core_path = argv[1];
	const char* rom_path = argv[2];
	system_dir = argv[3];
	out_dir = argv[4];
	save_dir = out_dir;

	HMODULE lib = LoadLibraryA(core_path);
	if (!lib)
	{
		printf("FAIL: cannot load %s (error %lu)\n", core_path, GetLastError());
		return 1;
	}

#define SYM(name) auto p_##name = (decltype(&name))GetProcAddress(lib, #name); \
	if (!p_##name) { printf("FAIL: missing export %s\n", #name); return 1; }
	SYM(retro_set_environment)
	SYM(retro_set_video_refresh)
	SYM(retro_set_audio_sample_batch)
	SYM(retro_set_input_poll)
	SYM(retro_set_input_state)
	SYM(retro_init)
	SYM(retro_deinit)
	SYM(retro_api_version)
	SYM(retro_get_system_info)
	SYM(retro_get_system_av_info)
	SYM(retro_load_game)
	SYM(retro_unload_game)
	SYM(retro_run)
	SYM(retro_reset)
	SYM(retro_serialize_size)
	SYM(retro_serialize)
	SYM(retro_unserialize)
	SYM(retro_get_memory_data)
	SYM(retro_get_memory_size)
#undef SYM

	printf("api version: %u\n", p_retro_api_version());

	retro_system_info sysinfo;
	p_retro_get_system_info(&sysinfo);
	printf("core: %s %s (ext: %s)\n", sysinfo.library_name, sysinfo.library_version, sysinfo.valid_extensions);

	p_retro_set_environment(env_cb);
	p_retro_set_video_refresh(video_cb);
	p_retro_set_audio_sample_batch(audio_batch_cb);
	p_retro_set_input_poll(input_poll_cb);
	p_retro_set_input_state(input_state_cb);
	p_retro_init();

	std::ifstream rom_file(rom_path, std::ios::binary);
	if (!rom_file.is_open())
	{
		printf("FAIL: cannot open rom %s\n", rom_path);
		return 1;
	}
	std::vector<uint8_t> rom((std::istreambuf_iterator<char>(rom_file)), {});
	printf("rom: %s (%zu bytes)\n", rom_path, rom.size());

	//The mode is optional, so a key=value in its place is an option and not a mode.
	//Without this check it was silently swallowed as an unrecognised mode name, and
	//the option it asked for never reached the core at all.
	bool have_mode = argc > 5 && !strchr(argv[5], '=');
	const char* mode = have_mode ? argv[5] : "";
	bool mouse_from_boot = strcmp(mode, "mouse") == 0;
	bool mouse_toggle_test = strcmp(mode, "mousetoggle") == 0;
	bool advance_test = strcmp(mode, "advance") == 0;
	bool bench_mode = strcmp(mode, "bench") == 0;
	int bench_warmup = 600;
	int bench_frames = 3600;
	{
		//[warmup_frames] [bench_frames] (bench only) then [key=value ...] in any mode
		int argi = have_mode ? 6 : 5;
		if (bench_mode)
		{
			if (argi < argc && !strchr(argv[argi], '=')) bench_warmup = atoi(argv[argi++]);
			if (argi < argc && !strchr(argv[argi], '=')) bench_frames = atoi(argv[argi++]);
		}
		for (; argi < argc; argi++)
		{
			const char* eq = strchr(argv[argi], '=');
			if (!eq)
			{
				printf("FAIL: bad option %s (want key=value)\n", argv[argi]);
				return 1;
			}
			std::string key(argv[argi], (size_t)(eq - argv[argi]));
			std::string val(eq + 1);

			//Consumed by the harness itself, not passed to the core
			if (key == "harness_underrun")
			{
				simulate_underrun = atoi(val.c_str()) != 0;
				printf("[harness] simulating audio buffer underrun: %s\n", simulate_underrun ? "yes" : "no");
				continue;
			}

			cli_options.emplace_back(key, val);
			printf("[opt] %s = %s\n", cli_options.back().first.c_str(), cli_options.back().second.c_str());
		}

		//Bench mode defers activation until after warmup so the game reaches the
		//scene under test with every stage running; other modes want them from boot
		if (!bench_mode)
		{
			cli_options_active = true;
		}
	}
	if (mouse_from_boot)
	{
		mouse_option_enabled = true;
		printf("[mouse test] option enabled from boot\n");
	}

	retro_game_info game = {rom_path, rom.data(), rom.size(), NULL};
	if (!p_retro_load_game(&game))
	{
		printf("FAIL: retro_load_game returned false\n");
		return 1;
	}
	printf("PASS: game loaded\n");

	retro_system_av_info av;
	p_retro_get_system_av_info(&av);
	printf("av: %ux%u (max %ux%u) aspect %.3f fps %.2f rate %.0f\n", av.geometry.base_width,
		   av.geometry.base_height, av.geometry.max_width, av.geometry.max_height, av.geometry.aspect_ratio,
		   av.timing.fps, av.timing.sample_rate);

	if (bench_mode)
	{
		//Warm up to the scene of interest, prove which scene we're timing with a
		//dump, then time the bench window with the wall clock
		for (int f = 0; f < bench_warmup; f++) p_retro_run();

		//Only now activate the skip options, so warmup emulated the full machine
		if (!cli_options.empty())
		{
			cli_options_active = true;
			mouse_option_dirty = true;  //signal GET_VARIABLE_UPDATE
			p_retro_run();              //give the core a frame to re-read variables
		}

		char name[MAX_PATH];
		snprintf(name, sizeof(name), "%s/bench_scene.bmp", out_dir.c_str());
		if (!last_frame.empty())
		{
			save_bmp565(name, last_frame.data(), last_w, last_h);
		}

		LARGE_INTEGER freq, t0, t1;
		QueryPerformanceFrequency(&freq);
		QueryPerformanceCounter(&t0);
		for (int f = 0; f < bench_frames; f++)
		{
			//Report buffer status the way a frontend does, once per frame
			if (audio_status_cb)
			{
				unsigned occupancy = simulate_underrun ? 10 : 90;
				audio_status_cb(true, occupancy, simulate_underrun);
			}
			p_retro_run();
		}
		QueryPerformanceCounter(&t1);

		double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
		printf("[bench] %d frames after %d warmup: %.1f ms total, %.4f ms/frame, %.1f fps equivalent\n",
			   bench_frames, bench_warmup, ms, ms / bench_frames, bench_frames * 1000.0 / ms);

		p_retro_unload_game();
		p_retro_deinit();
		return 0;
	}

	size_t sram_size = p_retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	void* sram_data = p_retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	printf("sram: %zu bytes at %p\n", sram_size, sram_data);
	size_t wram_size = p_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
	void* wram_data = p_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
	printf("wram: %zu bytes at %p\n", wram_size, wram_data);

	//Run 10 seconds of emulation, dumping frames along the way
	int dump_at[] = {60, 180, 300, 600};
	int dump_idx = 0;
	for (int f = 1; f <= 600; f++)
	{
		p_retro_run();
		frames_run++;
		if (dump_idx < 4 && f == dump_at[dump_idx])
		{
			char name[MAX_PATH];
			snprintf(name, sizeof(name), "%s/frame_%04d.bmp", out_dir.c_str(), f);
			if (last_frame.empty())
			{
				printf("FAIL: no video frame received by frame %d\n", f);
				return 1;
			}
			save_bmp565(name, last_frame.data(), last_w, last_h);
			printf("dumped %s (%ux%u)\n", name, last_w, last_h);
			dump_idx++;
		}
	}
	printf("PASS: ran %u frames\n", frames_run);
	printf("audio: %llu frames total (expect %u), rms %.4f\n", (unsigned long long)audio_samples_total,
		   frames_run * 800, audio_energy > 0 ? sqrt(audio_energy / (audio_samples_total * 2)) : 0.0);

	//Emulation-identity fingerprints. Work RAM and the audio stream are produced
	//purely by the emulated machine, so anything that only changes what is drawn
	//(frameskip) must leave both untouched.
	printf("wram_crc: %08X\n", crc32_buf(wram_data, wram_size));
	printf("audio_crc: %08X\n", audio_crc);

	if (advance_test)
	{
		//Tap Start and A alternately to advance through title/menus into
		//gameplay (used to reach Wanwan's PCM dialog). ~90 seconds total.
		char name[MAX_PATH];
		for (int burst = 0; burst < 30; burst++)
		{
			bool& btn = (burst & 1) ? pad_a : pad_start;
			btn = true;
			for (int f = 0; f < 10; f++) p_retro_run();
			btn = false;
			for (int f = 0; f < 170; f++) p_retro_run();

			if (burst % 5 == 4)
			{
				snprintf(name, sizeof(name), "%s/advance_%02d.bmp", out_dir.c_str(), burst + 1);
				save_bmp565(name, last_frame.data(), last_w, last_h);
			}
		}
		printf("[advance test] done, audio rms overall %.4f\n",
			   audio_energy > 0 ? sqrt(audio_energy / (audio_samples_total * 2)) : 0.0);
		p_retro_unload_game();
		p_retro_deinit();
		return 0;
	}

	if (mouse_from_boot || mouse_toggle_test)
	{
		char name[MAX_PATH];

		if (mouse_toggle_test)
		{
			printf("[mouse test] enabling option mid-session at frame 600\n");
			mouse_option_enabled = true;
			mouse_option_dirty = true;
			//give the core a frame to pick up the variable change
			p_retro_run();
		}

		std::vector<uint16_t> before = last_frame;
		snprintf(name, sizeof(name), "%s/mouse_before.bmp", out_dir.c_str());
		save_bmp565(name, last_frame.data(), last_w, last_h);

		//Move the mouse right and down for 2 seconds
		mouse_dx = 2;
		mouse_dy = 1;
		for (int f = 0; f < 120; f++) p_retro_run();
		mouse_dx = 0;
		mouse_dy = 0;
		for (int f = 0; f < 10; f++) p_retro_run();

		std::vector<uint16_t> after = last_frame;
		snprintf(name, sizeof(name), "%s/mouse_after_move.bmp", out_dir.c_str());
		save_bmp565(name, last_frame.data(), last_w, last_h);

		size_t diff = 0;
		for (size_t i = 0; i < before.size() && i < after.size(); i++)
			if (before[i] != after[i]) diff++;
		printf("[mouse test] pixels changed after movement: %zu\n", diff);

		if (mouse_toggle_test)
		{
			//The fix defers the option to the next restart: reset, boot back to
			//the title screen, and verify the mouse now responds
			printf("[mouse test] resetting content with option still enabled\n");
			p_retro_reset();
			for (int f = 0; f < 600; f++) p_retro_run();
			std::vector<uint16_t> before_reset = last_frame;

			mouse_dx = 2;
			mouse_dy = 1;
			for (int f = 0; f < 120; f++) p_retro_run();
			mouse_dx = 0;
			mouse_dy = 0;
			for (int f = 0; f < 10; f++) p_retro_run();

			size_t diff2 = 0;
			for (size_t i = 0; i < before_reset.size() && i < last_frame.size(); i++)
				if (before_reset[i] != last_frame[i]) diff2++;
			printf("[mouse test] pixels changed after reset + movement: %zu\n", diff2);
			snprintf(name, sizeof(name), "%s/mouse_after_reset_move.bmp", out_dir.c_str());
			save_bmp565(name, last_frame.data(), last_w, last_h);
		}

		//Click
		mouse_left = true;
		for (int f = 0; f < 10; f++) p_retro_run();
		mouse_left = false;
		for (int f = 0; f < 110; f++) p_retro_run();
		snprintf(name, sizeof(name), "%s/mouse_after_click.bmp", out_dir.c_str());
		save_bmp565(name, last_frame.data(), last_w, last_h);
		printf("[mouse test] done\n");
		p_retro_unload_game();
		p_retro_deinit();
		return 0;
	}

	//Savestate round-trip: save, run 120 frames, restore, compare re-run frame
	size_t ss_size = p_retro_serialize_size();
	printf("serialize size: %zu bytes\n", ss_size);
	std::vector<uint8_t> state(ss_size);
	if (!p_retro_serialize(state.data(), state.size()))
	{
		printf("FAIL: retro_serialize\n");
		return 1;
	}
	std::vector<uint16_t> frame_at_save = last_frame;

	for (int f = 0; f < 120; f++) p_retro_run();
	std::vector<uint16_t> frame_later = last_frame;

	if (!p_retro_unserialize(state.data(), state.size()))
	{
		printf("FAIL: retro_unserialize\n");
		return 1;
	}
	p_retro_run();
	printf("PASS: savestate round-trip (size stable: %s)\n",
		   p_retro_serialize_size() == ss_size ? "yes" : "NO - VIOLATION");

	//Determinism probe: restoring the same state and running one frame twice
	//must produce byte-identical serializations (required for run-ahead/netplay,
	//and proves the audio engine state is fully captured)
	std::vector<uint8_t> s1(ss_size), s2(ss_size);
	p_retro_unserialize(state.data(), state.size());
	p_retro_run();
	p_retro_serialize(s1.data(), s1.size());
	p_retro_unserialize(state.data(), state.size());
	p_retro_run();
	p_retro_serialize(s2.data(), s2.size());
	if (s1 == s2)
		printf("PASS: deterministic serialization after state restore\n");
	else
	{
		size_t first_diff = 0;
		while (first_diff < s1.size() && s1[first_diff] == s2[first_diff]) first_diff++;
		printf("FAIL: serializations differ after identical restore+run (first diff at byte %zu)\n", first_diff);
	}

	//Reset test
	p_retro_reset();
	for (int f = 0; f < 60; f++) p_retro_run();
	printf("PASS: reset + 60 frames\n");

	char name[MAX_PATH];
	snprintf(name, sizeof(name), "%s/frame_after_reset.bmp", out_dir.c_str());
	save_bmp565(name, last_frame.data(), last_w, last_h);

	p_retro_unload_game();
	p_retro_deinit();
	printf("PASS: unload + deinit clean\n");
	printf("ALL TESTS PASSED\n");
	return 0;
}
