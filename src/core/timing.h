#pragma once
#include <string>
#include <limits>
#include <cstdint>

#include "core/savestate.h"

namespace Timing
{

enum TimerId
{
	CPU_TIMER,
	NUM_TIMERS,
	INVALID_TIMER
};

//Raw function pointers, not std::function: every registrant is a plain
//static function (verified across all register_func/register_timer call
//sites), and this callback fires from the innermost scheduler loop -
//500+ times per emulated video frame at minimum - so avoiding
//std::function's type-erasure indirection here is worth it.
typedef void (*TimerFunc)();
typedef void (*EventFunc)(uint64_t, int);

/* Represents a registered function with a name. */
struct FuncHandle
{
	int value;

	FuncHandle() { value = -1; }
	bool is_valid() { return value >= 0; }
};

/* Represents a scheduled event for a particular core. */
struct EventHandle
{
	int64_t value;

	EventHandle() { value = -1; }
	bool is_valid() { return value >= 0; }

	int get_timer_id() { return value & 0xFF; }
	int64_t get_ev_id() { return value >> 8; }
};

//The clockrate of the CPU is exactly 16 MHz
constexpr static int F_CPU = 16 * 1000 * 1000;

//The VDP's own clock (NTSC), which sets the video timing: every frame is 263 lines
//of 1365 VDP cycles, so the console runs at 59.8261 Hz rather than 60 (megadoc,
//Raster Counters). A line is not a whole number of CPU cycles; see inc_vcount.
constexpr static int64_t F_VDP = 21477272;
constexpr static int VDP_CYCLES_PER_LINE = 1365;
constexpr static int LINES_PER_FRAME = 263;
constexpr static double HARDWARE_FRAME_RATE = (double)F_VDP / (VDP_CYCLES_PER_LINE * LINES_PER_FRAME);

//CPU cycles in one frame, measured on a console: 267,970 (about 1018.9 per line),
//0.2% more than the nominal clocks give. Only this ratio is used; F_CPU stays the
//timebase for everything else.
constexpr static int64_t CPU_CYCLES_PER_FRAME = 267970;

//The machine runs either at exactly 60 Hz or at the hardware's 59.8261 Hz (Video >
//Refresh Rate). Chosen before System::initialize and fixed for the session.
void set_hardware_refresh(bool hardware);
bool hardware_refresh();
double frame_rate();

//Maximum amount of time alloted to a slice
//TODO: make this bigger?
constexpr static int64_t MAX_SLICE_LENGTH = 512;

constexpr static int64_t MAX_TIMESTAMP = (std::numeric_limits<int64_t>::max)();

/* A scheduler cycle - a unit cycle is in units of the CPU's clockrate. */
enum class UnitCycle : int64_t;

void initialize();
void shutdown();

void register_timer(TimerId id, int32_t* cycle_count, TimerFunc func);

FuncHandle register_func(std::string name, EventFunc func);

EventHandle add_event(FuncHandle func, UnitCycle cycles, uint64_t param = 0, int core = -1);
void cancel_event(EventHandle& handle);

void process_slice(int id, int32_t slice);
int64_t calc_slice_length(int id);

int64_t get_timestamp(int id = -1);

//Cycles from now to where the register read in progress samples the state it
//returns (set by the bus around a device read, 0 otherwise)
extern int read_bias;
inline int64_t read_time() { return get_timestamp(CPU_TIMER) + read_bias; }
//Cycles the event being dispatched is running late by (0 outside an event)
int event_lateness();

UnitCycle convert_cpu(int64_t cycles);

void save_state(SaveState::Snapshot& ss);
void load_state(SaveState::Snapshot& ss);

template <int FREQ> UnitCycle convert(int64_t num)
{
	/* Check for overflow */
	int64_t max_value = MAX_TIMESTAMP / FREQ;

	if (num / FREQ > max_value)
	{
		/* Multiplication not possible, return largest possible value */
		return (UnitCycle)MAX_TIMESTAMP;
	}

	if (num > max_value)
	{
		/* Round down to prevent overflow */
		return (UnitCycle)((num / FREQ) * F_CPU);
	}

	return (UnitCycle)(num * F_CPU / FREQ);
}

}