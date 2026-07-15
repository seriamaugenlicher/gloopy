/*
Casio Loopy sound implementation by kasami, 2023-2024.
Features a reverse-engineered uPD937 synth engine, MIDI retiming, EQ filtering and resampling.

This implementation is INCOMPLETE, but mostly sufficient for Loopy emulation running original game
software. It is missing playback of the internal demo tune (used by some games) and rhythm presets
(not used) as the formats are currently unknown, and the synth core also lacks some small details.

The code is messy and will probably stay that way until a more complete implementation (standalone
uPD937 library?) replaces it in the future. It was ported from a Java prototype, and may have some
inefficiencies and things that aren't structured well for C++.

Game support notes:
- PC Collection title screen goes a bit fast and some sounds get stuck (timing issue?)
- Wanwan has no PCM sample support, and seems to crackle on dialog sfx (same timing issue?)
*/

#include <common/wordops.h>
#include <core/timing.h>
#include <log/log.h>
#include <sound/loopysound.h>
#include <sound/sound.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

namespace Sound
{

static Timing::FuncHandle timeref_func;
static Timing::EventHandle timeref_ev;

static std::unique_ptr<LoopySound::LoopySound> sound_engine;

static int sample_rate;
static int buffer_size;

static bool mute = false;
static float volume_level;	// Automatically managed by mute

static void buffer_callback(float* buffer, uint32_t count);

/* libretro-specific code start */

//The libretro frontend pulls exactly one video frame's worth of audio per
//retro_run, so there is no audio device or callback thread here. render()
//below is called synchronously by the libretro glue, which makes audio
//generation deterministic (a requirement for rewind/run-ahead/netplay).

//Decoded Wanwan expansion PCM, already converted to interleaved stereo
//float at the output rate. Replaces the SDL_AudioStream of the standalone.
static std::vector<float> wav_data;
static size_t wav_pos = 0;
static float wav_volume = 1;

static std::vector<float> mix_buffer;

void render(int16_t* output, uint32_t stereo_frames)
{
	uint32_t sample_count = stereo_frames * 2;
	if (mix_buffer.size() < sample_count)
	{
		mix_buffer.resize(sample_count);
	}

	//Synth output (zeroes the buffer if no sound engine is running)
	buffer_callback(mix_buffer.data(), sample_count);

	//Mix in queued expansion PCM, like the SDL callback used to
	for (uint32_t i = 0; i < sample_count && wav_pos < wav_data.size(); i++)
	{
		mix_buffer[i] += wav_data[wav_pos++] * wav_volume * volume_level;
	}
	if (wav_pos >= wav_data.size() && !wav_data.empty())
	{
		wav_data.clear();
		wav_pos = 0;
	}

	for (uint32_t i = 0; i < sample_count; i++)
	{
		float sample = std::clamp(mix_buffer[i], -1.f, 1.f);
		output[i] = (int16_t)(sample * 32767.f);
	}
}

/* libretro-specific code end */

static void timeref(uint64_t param, int cycles_late);

void initialize(std::vector<uint8_t>& sound_rom)
{
	if (!sound_rom.empty())
	{
		//No audio device to open: the frontend consumes whatever render()
		//produces. The engine's smoothing window is one video frame.
		sample_rate = TARGET_SAMPLE_RATE;
		buffer_size = SAMPLES_PER_FRAME;

		sound_engine = std::make_unique<LoopySound::LoopySound>(sound_rom, (float)sample_rate, buffer_size);

		if (TIMEREF_ENABLE)
		{
			Log::debug("[Sound] Schedule timeref %d Hz", TIMEREF_FREQUENCY);
			timeref_func = Timing::register_func("Sound::timeref", timeref);
			timeref(0, 0);
		}
	}
}

void shutdown()
{
	wav_data.clear();
	wav_pos = 0;
	sound_engine = nullptr;
}

uint8_t ctrl_read8(uint32_t addr)
{
	assert(0);
	return 0;
}

uint16_t ctrl_read16(uint32_t addr)
{
	assert(0);
	return 0;
}

uint32_t ctrl_read32(uint32_t addr)
{
	assert(0);
	return 0;
}

void ctrl_write8(uint32_t addr, uint8_t value)
{
	assert(0);
}

void ctrl_write16(uint32_t addr, uint16_t value)
{
	value &= 0xFFF;
	if (sound_engine)
	{
		sound_engine->set_control_register(value);
	}
}

void ctrl_write32(uint32_t addr, uint32_t value)
{
	WRITE_DOUBLEWORD(ctrl, addr, value);
}

void midi_byte_in(uint8_t value)
{
	//Log::debug("[Sound] MIDI byte %02X", value);
	//fflush(stdout);
	if (sound_engine)
	{
		sound_engine->midi_in((char)value);
	}
}

void set_mute(bool mute_in)
{
	mute = mute_in;
	Log::debug("[Sound] %s output", mute_in ? "Muted" : "Unmuted");
}

void set_mix_level(float level)
{
	if (sound_engine)
	{
		sound_engine->set_mix_level(level);
	}
}

static void timeref(uint64_t param, int cycles_late)
{
	constexpr static int cycles_per_timeref = Timing::F_CPU / TIMEREF_FREQUENCY;
	Timing::UnitCycle timeref_cycles = Timing::convert_cpu(cycles_per_timeref - cycles_late);
	timeref_ev = Timing::add_event(timeref_func, timeref_cycles, 0, Timing::CPU_TIMER);

	constexpr static float timeref_period = 1.f / TIMEREF_FREQUENCY;
	sound_engine->time_reference(timeref_period);
}

static void update_volume_level()
{
	if (MUTE_FADE_MS > 0)
	{
		float delta = 1000.f / (sample_rate * MUTE_FADE_MS);
		if (mute) delta = -delta;
		volume_level += delta;
		volume_level = std::clamp(volume_level, 0.f, 1.f);
	}
	else
	{
		volume_level = mute ? 0.f : 1.f;
	}
}

static void buffer_callback(float* sample_buffer, uint32_t sample_count)
{
	if (sound_engine)
	{
		// Generate samples if we can, updating the mute level every sample
		float tmp[2];
		int p = 0;
		for (uint32_t i = 0; i < sample_count / 2; i++)
		{
			update_volume_level();
			sound_engine->gen_sample(tmp);
			sample_buffer[p++] = tmp[0] * volume_level;
			sample_buffer[p++] = tmp[1] * volume_level;
		}
	}
	else
	{
		// If for some reason we can't generate samples, zero the buffer
		for (uint32_t i = 0; i < sample_count; i++)
		{
			sample_buffer[i] = 0.f;
		}
	}
}

//Minimal RIFF/WAVE reader for the expansion PCM files: PCM 8/16/24/32-bit
//or 32-bit float, mono or stereo, any rate. Replaces SDL_LoadWAV.
static bool load_wav_file(
	const std::string& path, std::vector<uint8_t>& raw, int& channels, int& rate, int& bits, bool& is_float
)
{
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open())
	{
		return false;
	}

	char riff[12];
	file.read(riff, 12);
	if (!file.good() || memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4))
	{
		return false;
	}

	bool have_fmt = false;
	uint16_t format_tag = 0;
	while (file.good())
	{
		char chunk_hdr[8];
		file.read(chunk_hdr, 8);
		if (!file.good()) break;
		uint32_t chunk_size;
		memcpy(&chunk_size, chunk_hdr + 4, 4);

		if (!memcmp(chunk_hdr, "fmt ", 4))
		{
			std::vector<uint8_t> fmt(chunk_size);
			file.read((char*)fmt.data(), chunk_size);
			if (chunk_size < 16) return false;
			memcpy(&format_tag, &fmt[0], 2);
			uint16_t ch;
			uint32_t sr;
			uint16_t bp;
			memcpy(&ch, &fmt[2], 2);
			memcpy(&sr, &fmt[4], 4);
			memcpy(&bp, &fmt[14], 2);
			//WAVE_FORMAT_EXTENSIBLE: the real format is in the extension
			if (format_tag == 0xFFFE && chunk_size >= 40)
			{
				memcpy(&format_tag, &fmt[24], 2);
			}
			channels = ch;
			rate = (int)sr;
			bits = bp;
			have_fmt = true;
		}
		else if (!memcmp(chunk_hdr, "data", 4))
		{
			raw.resize(chunk_size);
			file.read((char*)raw.data(), chunk_size);
			break;
		}
		else
		{
			file.seekg(chunk_size + (chunk_size & 1), std::ios::cur);
		}
	}

	if (!have_fmt || raw.empty()) return false;
	is_float = (format_tag == 3);
	if (format_tag != 1 && format_tag != 3) return false;
	if (is_float && bits != 32) return false;
	if (!is_float && bits != 8 && bits != 16 && bits != 24 && bits != 32) return false;
	if (channels != 1 && channels != 2) return false;
	return true;
}

static inline float wav_sample_to_float(const uint8_t* raw, size_t index, int bits, bool is_float)
{
	if (is_float)
	{
		float f;
		memcpy(&f, raw + index * 4, 4);
		return f;
	}
	if (bits == 32)
	{
		int32_t s;
		memcpy(&s, raw + index * 4, 4);
		return s / 2147483648.f;
	}
	if (bits == 24)
	{
		//Packed little-endian, sign-extend via the top byte
		const uint8_t* p = raw + index * 3;
		int32_t s = (p[0] << 8) | (p[1] << 16) | ((int32_t)(int8_t)p[2] << 24);
		return s / 2147483648.f;
	}
	if (bits == 16)
	{
		int16_t s;
		memcpy(&s, raw + index * 2, 2);
		return s / 32768.f;
	}
	//8-bit WAV is unsigned
	return (raw[index] - 128) / 128.f;
}

void wav_queue(std::string path, float volume)
{
	wav_volume = std::clamp(volume, 0.f, 1.f);

	std::vector<uint8_t> raw;
	int channels, rate, bits;
	bool is_float;
	if (!load_wav_file(path, raw, channels, rate, bits, is_float))
	{
		Log::error("[Sound] WAV failed to load at %s", path.c_str());
		return;
	}
	Log::debug("[Sound] WAV playing %s", path.c_str());

	//Convert to interleaved stereo float at the output rate (linear resampling),
	//appending to anything still queued (matches SDL_AudioStreamPut semantics)
	size_t src_frames = raw.size() / (channels * (bits / 8));
	if (src_frames == 0 || sample_rate <= 0) return;

	size_t out_frames = (size_t)((uint64_t)src_frames * sample_rate / rate);
	wav_data.reserve(wav_data.size() + out_frames * 2);
	for (size_t i = 0; i < out_frames; i++)
	{
		double src_pos = (double)i * rate / sample_rate;
		size_t i0 = (size_t)src_pos;
		size_t i1 = std::min(i0 + 1, src_frames - 1);
		float frac = (float)(src_pos - i0);

		for (int c = 0; c < 2; c++)
		{
			int src_c = (channels == 2) ? c : 0;
			float s0 = wav_sample_to_float(raw.data(), i0 * channels + src_c, bits, is_float);
			float s1 = wav_sample_to_float(raw.data(), i1 * channels + src_c, bits, is_float);
			wav_data.push_back(s0 + (s1 - s0) * frac);
		}
	}
}

void wav_stop()
{
	wav_data.clear();
	wav_pos = 0;
}

void save_state(SaveState::Snapshot& ss)
{
	ss.begin_section(SaveState::fourcc("SND "));
	uint8_t has_engine = (sound_engine != nullptr);
	ss.write(has_engine);

	//The engine blob is length-prefixed so a load can skip it when the
	//sound BIOS is present on one side only
	if (sound_engine)
	{
		SaveState::Snapshot engine_ss;
		sound_engine->save_state(engine_ss);
		uint32_t engine_size = (uint32_t)engine_ss.size();
		ss.write(engine_size);
		ss.write_blob(engine_ss.data(), engine_size);
	}
}

void load_state(SaveState::Snapshot& ss)
{
	ss.expect_section(SaveState::fourcc("SND "));
	uint8_t had_engine;
	ss.read(had_engine);

	if (had_engine)
	{
		uint32_t engine_size;
		ss.read(engine_size);
		if (sound_engine)
		{
			//Restore the full synth state: voices, instruments, envelopes,
			//MIDI parser and retiming queue all resume exactly
			sound_engine->load_state(ss);
		}
		else
		{
			//Saved with sound, running without: discard the engine state
			ss.skip(engine_size);
		}
	}
	else if (sound_engine)
	{
		//Saved without sound: nothing to restore. Silence held notes, and
		//re-kick the timeref event chain since it isn't in the restored
		//scheduler queue.
		sound_engine->silence();
		if (TIMEREF_ENABLE)
		{
			timeref(0, 0);
		}
	}
}

}  // namespace Sound
