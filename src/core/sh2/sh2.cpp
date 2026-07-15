#include <algorithm>
#include <cassert>
#include <cstring>

#ifdef LOOPY_PC_PROFILE
#include <unordered_map>
#include <utility>
#include <vector>
#endif

#include <log/log.h>

#include <common/bswp.h>

#include "core/sh2/peripherals/sh2_dmac.h"
#include "core/sh2/peripherals/sh2_intc.h"
#include "core/sh2/peripherals/sh2_pfc.h"
#include "core/sh2/peripherals/sh2_serial.h"
#include "core/sh2/peripherals/sh2_timers.h"
#include "core/sh2/sh2.h"
#include "core/sh2/sh2_bus.h"
#include "core/sh2/sh2_interpreter.h"
#include "core/sh2/sh2_local.h"
#include "core/memory.h"
#include "core/timing.h"

namespace SH2
{

CPU sh2;

#ifdef LOOPY_PC_PROFILE
static bool pc_profile_on;
static std::unordered_map<uint32_t, uint64_t> pc_hist;
static uint64_t pc_profile_total;

void pc_profile_enable(bool enable)
{
	if (enable && !pc_profile_on)
	{
		pc_hist.clear();
		pc_profile_total = 0;
	}
	pc_profile_on = enable;
}

void pc_profile_report()
{
	if (!pc_profile_total)
	{
		return;
	}

	std::vector<std::pair<uint32_t, uint64_t>> sorted(pc_hist.begin(), pc_hist.end());
	std::sort(sorted.begin(), sorted.end(), [](auto& l, auto& r) { return l.second > r.second; });

	Log::info("[pcprof] %llu instructions executed across %llu distinct PCs",
			  (unsigned long long)pc_profile_total, (unsigned long long)pc_hist.size());

	//Hot PCs, with the opcode word at each so a busy-wait loop is recognizable
	uint64_t top_sum = 0;
	int shown = (int)std::min<size_t>(sorted.size(), 40);
	for (int i = 0; i < shown; i++)
	{
		uint32_t addr = sorted[i].first;
		uint64_t count = sorted[i].second;
		top_sum += count;
		Log::info("[pcprof] #%02d %08X  %04X  %10llu  %5.2f%%", i + 1, addr, Bus::read16(addr),
				  (unsigned long long)count, 100.0 * (double)count / (double)pc_profile_total);
	}
	Log::info("[pcprof] top %d PCs account for %.2f%% of all executed instructions", shown,
			  100.0 * (double)top_sum / (double)pc_profile_total);

	//The hottest PC is almost certainly inside the dominant loop: dump a window
	//around it so the loop body can be read directly
	if (!sorted.empty())
	{
		uint32_t hot = sorted[0].first;
		uint32_t start = hot - 16;
		Log::info("[pcprof] window around hottest PC %08X:", hot);
		for (uint32_t a = start; a <= hot + 16; a += 2)
		{
			auto it = pc_hist.find(a);
			uint64_t count = (it != pc_hist.end()) ? it->second : 0;
			Log::info("[pcprof]   %08X  %04X  %10llu%s", a, Bus::read16(a), (unsigned long long)count,
					  a == hot ? "  <== hottest" : "");
		}
	}
}
#endif

static bool can_accept_exception(int vector_id, int prio)
{
	int imask = (sh2.sr >> 4) & 0xF;
	if (imask == 0xF)
	{
		return false;
	}
	return prio > imask;
}

static bool can_execute_exception(int vector_id, int prio)
{
	//Some types not accepted after certain instructions (SH7021 datasheet tables 4.9 & 4.2)
	bool is_address_error = (vector_id >= 9 && vector_id <= 10);
	bool is_interrupt = (vector_id >= 11 && vector_id <= 12) || (vector_id >= 64);

	//Our implementation of the pipeline explodes if we allow any exception
	//right after the pipeline became invalid. This fixes it.
	if (!sh2.pipeline_valid)
	{
		return false;
	}
	if (sh2.in_delay_slot && (is_address_error || is_interrupt))
	{
		return false;
	}
	if (sh2.in_nointerrupt_slot && is_interrupt)
	{
		return false;
	}

	return true;
}

void raise_exception(int vector_id);

static bool handle_exception()
{
	if (sh2.pending_exception_vector)
	{
		int vector = sh2.pending_exception_vector;
		int prio = sh2.pending_exception_prio;
		if (can_execute_exception(vector, prio))
		{
			raise_exception(vector);
		
			int new_imask = std::clamp(prio, 0, 15);
		
			//Interrupt mask should only be modified after the above function so that the original value can be pushed onto the stack
			sh2.sr &= ~0xF0;
			sh2.sr |= new_imask << 4;

			sh2.pending_exception_vector = 0;
			return true;
		}
	}
	return false;
}

void initialize()
{
	sh2 = {};

	sh2.pagetable = Memory::get_sh2_pagetable();

	//TODO: make config option to skip BIOS boot?
	bool skip_bios_boot = false;
	if (skip_bios_boot)
	{
		set_pc(0x0E000480);
		sh2.gpr[15] = 0;
	}
	else
	{
		//The initial values of PC and SP are read from the vector table
		int boot_type = 0;
		uint8_t* boot_vectors = sh2.pagetable[0];
		uint32_t reset_pc, reset_sp;
		memcpy(&reset_pc, boot_vectors + boot_type*8 + 0, 4);
		memcpy(&reset_sp, boot_vectors + boot_type*8 + 4, 4);
		set_pc(Common::bswp32(reset_pc));
		sh2.gpr[15] = Common::bswp32(reset_sp);
	}

	//Next, VBR is cleared to zero and interrupt mask bits in SR are set to 1111
	sh2.vbr = 0;
	sh2.sr |= 0xF << 4;

	//Initialize pipeline & execution state
	sh2.pipeline_valid = false;
	sh2.in_delay_slot = false;
	sh2.in_nointerrupt_slot = false;
	sh2.fetch_cycles = 1;
	sh2.fetch_cache_page = 1;  //invalid sentinel (never page-aligned)

	Timing::register_timer(Timing::CPU_TIMER, &sh2.cycles_left, run);

	//Set up on-chip peripheral modules after CPU is done
	OCPM::DMAC::initialize();
	OCPM::INTC::initialize();
	OCPM::PFC::initialize();
	OCPM::Serial::initialize();
	OCPM::Timer::initialize();
}

static void recompute_hook_range();

void shutdown()
{
	sh2.hooks.clear();
	recompute_hook_range();
}

static bool idle_skip_enabled = true;

void set_idle_skip(bool enable)
{
	idle_skip_enabled = enable;
}

//Registers whose equality across a loop iteration proves the iteration was a
//no-op. Everything architecturally visible to the loop body is included.
static void idle_take_snapshot()
{
	memcpy(sh2.idle_snapshot, sh2.gpr, sizeof(sh2.gpr));
	sh2.idle_snapshot[16] = sh2.pr;
	sh2.idle_snapshot[17] = sh2.macl;
	sh2.idle_snapshot[18] = sh2.mach;
	sh2.idle_snapshot[19] = sh2.gbr;
	sh2.idle_snapshot[20] = sh2.vbr;
	sh2.idle_snapshot[21] = sh2.sr;
}

static bool idle_snapshot_matches()
{
	return !memcmp(sh2.idle_snapshot, sh2.gpr, sizeof(sh2.gpr)) && sh2.idle_snapshot[16] == sh2.pr &&
		   sh2.idle_snapshot[17] == sh2.macl && sh2.idle_snapshot[18] == sh2.mach &&
		   sh2.idle_snapshot[19] == sh2.gbr && sh2.idle_snapshot[20] == sh2.vbr && sh2.idle_snapshot[21] == sh2.sr;
}

//Only loops this short are considered. The known wait loops span 6-10 bytes;
//the bound keeps the detector off the back edge of ordinary long loops.
constexpr static uint32_t IDLE_MAX_SPAN = 64;

void run()
{
	//Note: last_instruction_done from the original per-cycle loop is elided -
	//it was always true (upstream TODO: wait on longer instructions like multiply)

	//The idle detector is rebuilt every timeslice. This is what makes the skip
	//safe: a fixpoint may only be established from iterations executed *within*
	//the current slice, i.e. after the events that ended the previous one have
	//been applied. Carrying detection state across a slice boundary would let
	//the CPU skip past a change it had not yet observed.
	sh2.idle_prev_addr = 0xFFFFFFFF;
	sh2.idle_armed = false;
	sh2.idle_snapshot_valid = false;
	sh2.idle_wrote_mem = false;
	sh2.idle_unsafe_read = false;

	while (sh2.cycles_left > 0)
	{
		//Idle-loop skip, evaluated at an instruction boundary before any state
		//for this iteration is touched, so bailing out here leaves the CPU
		//parked cleanly at the loop head with its pipeline intact.
		if (idle_skip_enabled && sh2.pipeline_valid)
		{
			uint32_t addr = sh2.pipeline_src_addr;

			//A short backward jump lands on a loop head. Never skip out of a delay
			//slot, and never skip while an exception is pending - that one would
			//deadlock the CPU by deferring the very interrupt it is waiting on.
			if (addr < sh2.idle_prev_addr && (sh2.idle_prev_addr - addr) <= IDLE_MAX_SPAN && !sh2.in_delay_slot &&
				!sh2.in_nointerrupt_slot && !sh2.pending_exception_vector)
			{
				bool had_side_effects = sh2.idle_wrote_mem || sh2.idle_unsafe_read;

				if (sh2.idle_armed && addr == sh2.idle_head)
				{
					if (had_side_effects)
					{
						//The iteration just executed changed something outside the
						//loop, so no fixpoint can span it. Dropping the snapshot
						//(rather than refreshing it) both keeps tight write loops
						//- memset, blits - free of snapshot cost, and prevents a
						//stale snapshot from ever being compared across an
						//iteration that had side effects.
						sh2.idle_snapshot_valid = false;
					}
					else if (sh2.idle_snapshot_valid && idle_snapshot_matches())
					{
						//A full iteration wrote nothing, read nothing that can
						//change on its own, and left every register identical: it
						//will do so forever. Nothing can change until the next
						//scheduler event, and the slice ends at that event, so
						//consume the remainder of it in one step.
						sh2.cycles_left = 0;
						return;
					}
					else
					{
						sh2.idle_snapshot_valid = true;
						idle_take_snapshot();
					}
				}
				else
				{
					sh2.idle_head = addr;
					sh2.idle_armed = true;
					sh2.idle_snapshot_valid = true;
					idle_take_snapshot();
				}

				sh2.idle_wrote_mem = false;
				sh2.idle_unsafe_read = false;
			}

			sh2.idle_prev_addr = addr;
		}

		//Burn all remaining fetch-wait cycles in one step. The original loop
		//iterated once per emulated clock (16 million times per second),
		//spending most iterations only decrementing the fetch counter; this
		//consumes them arithmetically with identical cycle accounting.
		int32_t wait_cycles = sh2.fetch_cycles - 1;
		if (wait_cycles < 0)
		{
			wait_cycles = 0;
		}
		if (wait_cycles >= sh2.cycles_left)
		{
			//Fetch does not complete within this slice
			sh2.fetch_cycles -= sh2.cycles_left;
			sh2.cycles_left = 0;
			return;
		}
		sh2.cycles_left -= wait_cycles;

		//Handle any pending exceptions first, this may change the following fetch
		handle_exception();

		//Start the next fetch with the current PC. Fast path: reuse the
		//cached backing pointer and cycle cost while execution stays within
		//the same 4KB page (avoids two address translations per instruction).
		uint32_t fetch_src_addr = sh2.pc;
		uint16_t fetch_instruction;
		if ((fetch_src_addr & ~0xFFFu) == sh2.fetch_cache_page)
		{
			uint16_t raw;
			memcpy(&raw, sh2.fetch_cache_base + (fetch_src_addr & 0xFFF), 2);
			fetch_instruction = Common::bswp16(raw);
			sh2.fetch_cycles = sh2.fetch_cache_cycles;
		}
		else
		{
			fetch_instruction = Bus::read16(fetch_src_addr);
			sh2.fetch_cycles = Bus::read_cycles(fetch_src_addr);

			uint8_t* base = Bus::page_ptr(fetch_src_addr);
			if (base)
			{
				sh2.fetch_cache_page = fetch_src_addr & ~0xFFFu;
				sh2.fetch_cache_base = base;
				sh2.fetch_cache_cycles = sh2.fetch_cycles;
			}
		}
		sh2.fetch_done = false;

		//Advance the pipeline
		uint32_t execute_src_addr = sh2.pipeline_src_addr;
		uint16_t execute_instruction = sh2.pipeline_instruction;
		bool execute_valid = sh2.pipeline_valid;
		sh2.pipeline_src_addr = fetch_src_addr;
		sh2.pipeline_instruction = fetch_instruction;
		sh2.pipeline_valid = true;
		sh2.pc += 2;

		//Hooks exist only at a few fixed BIOS addresses; two compares skip
		//the map lookup for all other code (this lookup previously ran per
		//executed instruction)
		if (execute_src_addr >= sh2.hook_min && execute_src_addr <= sh2.hook_max)
		{
			auto hook = sh2.hooks.find(execute_src_addr);

			//If hook returns true, the actual instruction is skipped
			if (hook != sh2.hooks.end() && hook->second(execute_src_addr))
			{
				execute_valid = false;
			}
		}

		//Execute whatever just came off the pipeline
		bool was_delay_slot = sh2.in_delay_slot;
		bool was_nointerrupt_slot = sh2.in_nointerrupt_slot;
		if (execute_valid)
		{
#ifdef LOOPY_PC_PROFILE
			if (pc_profile_on)
			{
				pc_hist[execute_src_addr]++;
				pc_profile_total++;
			}
#endif
			SH2::Interpreter::run(execute_instruction, execute_src_addr);
		}
		//This should probably be done more directly in the interpreter
		if (was_delay_slot)
		{
			sh2.in_delay_slot = false;
		}
		if (was_nointerrupt_slot)
		{
			sh2.in_nointerrupt_slot = false;
		}

		sh2.cycles_left -= 1;
	}
}

void assert_irq(int vector_id, int prio)
{
	if (!can_accept_exception(vector_id, prio))
	{
		return;
	}
	sh2.pending_exception_vector = vector_id;
	sh2.pending_exception_prio = prio;
}

void raise_exception(int vector_id)
{
	assert(vector_id < 0x100);

	//Push SR and PC onto the stack
	sh2.gpr[15] -= 4;
	Bus::write32(sh2.gpr[15], sh2.sr);
	sh2.gpr[15] -= 4;
	Bus::write32(sh2.gpr[15], sh2.pc - 2);

	uint32_t vector_addr = sh2.vbr + (vector_id * 4);
	uint32_t new_pc = Bus::read32(vector_addr);

	set_pc(new_pc);
	sh2.pipeline_valid = false;
}

void set_pc(uint32_t new_pc)
{
	sh2.pc = new_pc;
}

void set_sr(uint32_t new_sr)
{
	sh2.sr = new_sr & 0x3F3;
}

void save_state(SaveState::Snapshot& ss)
{
	ss.begin_section(SaveState::fourcc("SH2C"));

	ss.write(sh2.gpr);
	ss.write(sh2.pc);
	ss.write(sh2.pr);
	ss.write(sh2.macl);
	ss.write(sh2.mach);
	ss.write(sh2.gbr);
	ss.write(sh2.vbr);
	ss.write(sh2.sr);
	ss.write(sh2.cycles_left);
	ss.write(sh2.pending_exception_prio);
	ss.write(sh2.pending_exception_vector);
	ss.write(sh2.fetch_done);
	ss.write(sh2.fetch_cycles);
	ss.write(sh2.pipeline_src_addr);
	ss.write(sh2.pipeline_instruction);
	ss.write(sh2.pipeline_valid);
	ss.write(sh2.in_delay_slot);
	ss.write(sh2.in_nointerrupt_slot);
}

void load_state(SaveState::Snapshot& ss)
{
	ss.expect_section(SaveState::fourcc("SH2C"));

	//The pagetable and hooks belong to the current session and are not serialized
	ss.read(sh2.gpr);
	ss.read(sh2.pc);
	ss.read(sh2.pr);
	ss.read(sh2.macl);
	ss.read(sh2.mach);
	ss.read(sh2.gbr);
	ss.read(sh2.vbr);
	ss.read(sh2.sr);
	ss.read(sh2.cycles_left);
	ss.read(sh2.pending_exception_prio);
	ss.read(sh2.pending_exception_vector);
	ss.read(sh2.fetch_done);
	ss.read(sh2.fetch_cycles);
	ss.read(sh2.pipeline_src_addr);
	ss.read(sh2.pipeline_instruction);
	ss.read(sh2.pipeline_valid);
	ss.read(sh2.in_delay_slot);
	ss.read(sh2.in_nointerrupt_slot);

	//The fetch fast-path cache is session state, not machine state
	sh2.fetch_cache_page = 1;

	//Likewise the idle detector: it is rebuilt from scratch on entry to every
	//timeslice, so a loaded state never inherits a stale fixpoint
	sh2.idle_armed = false;
	sh2.idle_snapshot_valid = false;
}

static void recompute_hook_range()
{
	sh2.hook_min = 0xFFFFFFFF;
	sh2.hook_max = 0;
	for (const auto& entry : sh2.hooks)
	{
		sh2.hook_min = std::min(sh2.hook_min, entry.first);
		sh2.hook_max = std::max(sh2.hook_max, entry.first);
	}
}

void add_hook(uint32_t address, HookFunc hook)
{
	sh2.hooks.emplace(address, hook);
	recompute_hook_range();
}

void remove_hook(uint32_t address)
{
	sh2.hooks.erase(address);
	recompute_hook_range();
}

}