#include <algorithm>
#include <cassert>
#include <cstring>

#include <log/log.h>

#include <common/bswp.h>

#include "core/sh2/peripherals/sh2_dmac.h"
#include "core/sh2/peripherals/sh2_intc.h"
#include "core/sh2/peripherals/sh2_pfc.h"
#include "core/sh2/peripherals/sh2_serial.h"
#include "core/sh2/peripherals/sh2_wdt.h"
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

//Whether the INTC's request gets past SR.IMASK (NMI, level 16, always does)
static bool can_accept_exception(int vector_id, int prio)
{
	if (!vector_id)
	{
		return false;
	}
	int imask = (sh2.sr >> 4) & 0xF;
	return prio > imask;
}

//Interrupt timing, measured on a console: a new request is taken INTERRUPT_LATENCY
//states after the INTC presents it, at the next instruction boundary. The exception
//sequence is the manual's 5 + m1 + m2 + m3 states (SR and PC pushes, vector read).
constexpr int INTERRUPT_LATENCY = 7;
constexpr int INTERRUPT_ENTRY_STATES = 5;
constexpr int SLEEP_WAKE_STATES = 2;

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
	//A request the INTC has just started presenting takes INTERRUPT_LATENCY states
	//to get through its priority decision and the mask comparison
	if (is_interrupt && cpu_now() - sh2.request_time < INTERRUPT_LATENCY)
	{
		return false;
	}

	return true;
}

void raise_exception(int vector_id);

//Load-use interlock: the next instruction waits a state if it reads or writes the
//register a load just wrote. These decode just enough of the opcode map for that.

//The general register an instruction loads from memory, or -1
static int loaded_gpr(uint16_t instr)
{
	int n = (instr >> 8) & 0xF;
	switch (instr >> 12)
	{
	case 0x0:
		//MOV.B/W/L @(R0,Rm),Rn
		return ((instr & 0xF) >= 0xC && (instr & 0xF) <= 0xE) ? n : -1;
	case 0x5:
		//MOV.L @(disp,Rm),Rn
		return n;
	case 0x6:
		//MOV.B/W/L @Rm,Rn and @Rm+,Rn
		return ((instr & 0xF) <= 0x2 || ((instr & 0xF) >= 0x4 && (instr & 0xF) <= 0x6)) ? n : -1;
	case 0x8:
		//MOV.B/W @(disp,Rm),R0
		return (n == 0x4 || n == 0x5) ? 0 : -1;
	case 0x9:
	case 0xD:
		//MOV.W/L @(disp,PC),Rn
		return n;
	case 0xC:
		//MOV.B/W/L @(disp,GBR),R0
		return (n >= 0x4 && n <= 0x6) ? 0 : -1;
	default:
		return -1;
	}
}

//Every general register an instruction reads or writes, as a bit mask. Slightly
//generous where an encoding group mixes forms, never short.
static uint32_t gpr_use_mask(uint16_t instr)
{
	uint32_t n = 1u << ((instr >> 8) & 0xF);
	uint32_t m = 1u << ((instr >> 4) & 0xF);
	constexpr uint32_t R0 = 1;
	switch (instr >> 12)
	{
	case 0x0:
		switch (instr & 0xF)
		{
		case 0x4: case 0x5: case 0x6: case 0x7:
		case 0xC: case 0xD: case 0xE: case 0xF:
			return R0 | n | m;
		case 0x2: case 0x3: case 0x9: case 0xA:
			return n;
		default:
			return 0;
		}
	case 0x1: case 0x2: case 0x3: case 0x5: case 0x6:
		return n | m;
	case 0x4:
		return ((instr & 0xF) == 0xF) ? (n | m) : n;
	case 0x7: case 0x9: case 0xD: case 0xE:
		return n;
	case 0x8:
		switch ((instr >> 8) & 0xF)
		{
		case 0x0: case 0x1: case 0x4: case 0x5:
			return R0 | m;
		case 0x8:
			return R0;
		default:
			return 0;
		}
	case 0xC:
		return R0;
	default:
		return 0;
	}
}

//CPU address error, taken like an interrupt's sequence (no request latency)
static void take_address_error(uint32_t return_pc)
{
	sh2.exec_external = true;
	sh2.in_execute = true;
	raise_exception_to(9, return_pc);
	sh2.in_execute = false;
	sh2.cycles_left -= INTERRUPT_ENTRY_STATES;
}

static bool handle_exception()
{
	//The request stays presented while it is masked, so it is taken as soon as
	//SR.IMASK drops (RTE, LDC) rather than lost
	if (can_accept_exception(sh2.pending_exception_vector, sh2.pending_exception_prio))
	{
		int vector = sh2.pending_exception_vector;
		int prio = sh2.pending_exception_prio;
		if (can_execute_exception(vector, prio))
		{
			//The exception replaces the instruction in decode, and the next one's
			//fetch is wasted: free at 4n+2 on the internal bus; on the external bus
			//one cycle less, except after an RTE, whose fetch has only begun
			uint32_t next = sh2.pc;
			uint32_t area = (next >> 24) & 0xF;
			int wasted_fetch = (area == 0x0 || area == 0x8 || area == 0xF) ? ((next & 2) ? 0 : 1)
				: Bus::read_cycles(next) - (sh2.rte_return ? 0 : 1);
			sh2.oram_trail = 0;
			if (area == 0x1 || area == 0x9)
			{
				wasted_fetch += Bus::dram_fetch_extra(next);
			}
			sh2.exec_external = true;
			sh2.in_execute = true;
			raise_exception(vector);
			sh2.in_execute = false;
			sh2.cycles_left -= INTERRUPT_ENTRY_STATES + wasted_fetch;
		
			int new_imask = std::clamp(prio, 0, 15);
		
			//Interrupt mask should only be modified after the above function so that the original value can be pushed onto the stack
			sh2.sr &= ~0xF0;
			sh2.sr |= new_imask << 4;

			//Tells the INTC to consume an edge request; it presents whatever is
			//left, which the raised IMASK now holds off
			OCPM::INTC::acknowledge();
			return true;
		}
	}
	return false;
}

void initialize()
{
	sh2 = {};

	sh2.pagetable = Memory::get_sh2_pagetable();

	//The initial values of PC and SP are read from the vector table
	uint8_t* boot_vectors = sh2.pagetable[0];
	uint32_t reset_pc, reset_sp;
	memcpy(&reset_pc, boot_vectors + 0, 4);
	memcpy(&reset_sp, boot_vectors + 4, 4);
	set_pc(Common::bswp32(reset_pc));
	sh2.gpr[15] = Common::bswp32(reset_sp);

	//Next, VBR is cleared to zero and interrupt mask bits in SR are set to 1111
	sh2.vbr = 0;
	sh2.sr |= 0xF << 4;

	//Initialize pipeline & execution state
	sh2.pipeline_valid = false;
	sh2.in_delay_slot = false;
	sh2.in_nointerrupt_slot = false;
	sh2.fetch_cycles = 1;
	sh2.fetch_cache_page = 1;  //invalid sentinel (never page-aligned)
	sh2.dram_row = 0xFFFFFFFF;
	sh2.dram_last_access = -1;
	sh2.port_access_cycles = 0;
	sh2.mul_issue_free = 0;
	sh2.mul_result_ready = 0;
	sh2.dram_last_end = -1;
	sh2.dram_last_was_write = false;
	sh2.load_reg = -1;

	Timing::register_timer(Timing::CPU_TIMER, &sh2.cycles_left, run);

	//Set up on-chip peripheral modules after CPU is done
	OCPM::DMAC::initialize();
	OCPM::INTC::initialize();
	OCPM::PFC::initialize();
	OCPM::Serial::initialize();
	OCPM::WDT::initialize();
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
	sh2.slice_start_time = Timing::get_timestamp(Timing::CPU_TIMER);
	sh2.slice_entry_cycles = sh2.cycles_left;

	sh2.idle_prev_addr = 0xFFFFFFFF;
	sh2.idle_armed = false;
	sh2.idle_snapshot_valid = false;
	sh2.idle_wrote_mem = false;
	sh2.idle_unsafe_read = false;

	while (sh2.cycles_left > 0)
	{
		//A running DMA transfer takes the bus when it is its turn
		if (sh2.dma_busy && OCPM::DMAC::run(sh2.sleeping))
		{
			continue;
		}

		//SLEEP: nothing runs until an interrupt can be taken. Its request latency
		//still applies; without a request the CPU idles to the slice's end, where
		//the event that may bring one happens.
		if (sh2.sleeping)
		{
			if (!can_accept_exception(sh2.pending_exception_vector, sh2.pending_exception_prio))
			{
				//A DMA transfer still starting runs once its start-up is over
				if (sh2.dma_busy)
				{
					int64_t until = OCPM::DMAC::next_start() - cpu_now();
					if (until < sh2.cycles_left)
					{
						sh2.cycles_left -= (int32_t)std::max<int64_t>(until, 1);
						continue;
					}
				}
				sh2.cycles_left = 0;
				return;
			}
			int64_t wait = sh2.request_time + INTERRUPT_LATENCY - cpu_now();
			if (wait > 0)
			{
				sh2.cycles_left -= (int32_t)std::min<int64_t>(wait, sh2.cycles_left);
				continue;
			}
			//Waking takes 2 states more than interrupting a running CPU (measured)
			sh2.sleeping = false;
			sh2.cycles_left -= SLEEP_WAKE_STATES;
		}

		//Idle-loop skip, evaluated at an instruction boundary before any state
		//for this iteration is touched, so bailing out here leaves the CPU
		//parked cleanly at the loop head with its pipeline intact.
		if (idle_skip_enabled && sh2.pipeline_valid && !sh2.dma_busy)
		{
			uint32_t addr = sh2.pipeline_src_addr;

			//A short backward jump lands on a loop head. Never skip out of a delay
			//slot, and never skip while an exception is pending - that one would
			//deadlock the CPU by deferring the very interrupt it is waiting on.
			if (addr < sh2.idle_prev_addr && (sh2.idle_prev_addr - addr) <= IDLE_MAX_SPAN && !sh2.in_delay_slot &&
				!sh2.in_nointerrupt_slot &&
				!can_accept_exception(sh2.pending_exception_vector, sh2.pending_exception_prio))
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

		//A fetch from an odd address raises an address error once the instruction
		//before it (a delay slot) has run, with the odd address stacked
		if (sh2.fetch_address_error)
		{
			sh2.fetch_address_error = false;
			take_address_error(sh2.fetch_error_pc);
		}

		//Handle any pending exceptions first, this may change the following fetch
		handle_exception();
		sh2.cycles_left -= sh2.oram_trail;
		sh2.oram_trail = 0;

		//Start the next fetch with the current PC. Fast path: reuse the
		//cached backing pointer and cycle cost while execution stays within
		//the same 4KB page (avoids two address translations per instruction).
		uint32_t fetch_src_addr = sh2.pc;
		if (fetch_src_addr & 1)
		{
			sh2.fetch_address_error = true;
			sh2.fetch_error_pc = fetch_src_addr;
		}
		uint16_t fetch_instruction;
		if ((fetch_src_addr & ~0xFFFu) == sh2.fetch_cache_page)
		{
			uint16_t raw;
			//An odd PC fetches the aligned halfword, never a byte past the page;
			//the address error is taken before it would execute
			memcpy(&raw, sh2.fetch_cache_base + (fetch_src_addr & 0xFFE), 2);
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
				//Work RAM: area 1, and its mirror at 9 (bit 27 is ignored)
				sh2.fetch_cache_dram = ((fetch_src_addr >> 24) & 0x7) == 0x1 &&
					((fetch_src_addr >> 24) & 0xF) != 0xF;
			}
		}
		if (sh2.fetch_cache_dram && (fetch_src_addr & ~0xFFFu) == sh2.fetch_cache_page)
		{
			//Code in work RAM: the DRAM row and refresh apply to fetches too
			sh2.fetch_cycles += Bus::dram_fetch_extra(fetch_src_addr);
		}

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
			//BIOS ROM (area 0) and on-chip RAM (area F) sit on the CPU's internal
			//bus; everything else was fetched over the external one
			uint32_t area = (execute_src_addr >> 24) & 0xF;
			sh2.exec_external = area != 0x0 && area != 0x8 && area != 0xF;
			//Only internal-bus code shows the stall: on the external bus the slot
			//does a fetch that was due anyway (measured). It overlaps a wait for the
			//fetch after the previous instruction's read on the internal bus (below)
			if (!sh2.exec_external && sh2.load_reg >= 0 && (gpr_use_mask(execute_instruction) >> sh2.load_reg) & 1 &&
				!sh2.port_fetch_stall)
			{
				sh2.cycles_left -= 1;
			}
			sh2.port_fetch_stall = false;

			sh2.read_prev_instr = sh2.read_this_instr;
			sh2.read_this_instr = false;
			sh2.wram_read_prev_instr = sh2.wram_read_this_instr;
			sh2.wram_read_this_instr = false;
			if (!sh2.in_delay_slot)
			{
				sh2.rte_return = false;
			}
			sh2.in_execute = true;
			SH2::Interpreter::run(execute_instruction, execute_src_addr);
			sh2.in_execute = false;
			//A misaligned data access happened at the aligned address; the address
			//error follows with the instruction 4 bytes on stacked
			if (sh2.data_address_error)
			{
				sh2.data_address_error = false;
				take_address_error(sh2.pc);
			}
			sh2.load_reg = loaded_gpr(execute_instruction);

			//Internal-bus code shares one memory port between data accesses and the
			//fetch, which reads two instructions at a time in the slot of the one at
			//4n+2: an access there serialises with it, elsewhere it overlaps the
			//instruction's cycle. A load-use stall right after an on-chip peripheral
			//or on-chip RAM read at 4n+2 overlaps its wait (measured).
			if (!sh2.exec_external && sh2.port_access_cycles)
			{
				int fetch_here = (execute_src_addr & 2) ? 1 : 0;
				sh2.port_fetch_stall = fetch_here && sh2.port_internal_read_only;
				sh2.cycles_left -= sh2.port_access_cycles + fetch_here - 1;
				sh2.port_access_cycles = 0;
			}
			sh2.port_internal_read_only = false;
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

		//Cycle stealing: the DMAC takes a unit for each bus access the CPU made
		if (sh2.dma_busy)
		{
			OCPM::DMAC::cpu_accessed(1 + sh2.bus_transfers);
		}
		sh2.bus_transfers = 0;
	}
}

//The INTC's current request, or vector 0 for none, recorded whether or not SR
//masks it. A request raised by an event dates from when the event was due.
void assert_irq(int vector_id, int prio)
{
	if (vector_id != sh2.pending_exception_vector || prio != sh2.pending_exception_prio)
	{
		sh2.request_time = Timing::get_timestamp(Timing::CPU_TIMER) - Timing::event_lateness();
	}
	sh2.pending_exception_vector = vector_id;
	sh2.pending_exception_prio = prio;
}

void raise_exception(int vector_id)
{
	raise_exception_to(vector_id, sh2.pc - 2);
}

void raise_exception_to(int vector_id, uint32_t return_pc)
{
	assert(vector_id < 0x100);

	//Push SR and PC onto the stack
	sh2.in_exception_sequence = true;
	sh2.exception_pushed = false;
	sh2.gpr[15] -= 4;
	Bus::write32(sh2.gpr[15], sh2.sr);
	sh2.exception_pushed = true;
	sh2.gpr[15] -= 4;
	Bus::write32(sh2.gpr[15], return_pc);

	uint32_t vector_addr = sh2.vbr + (vector_id * 4);
	uint32_t new_pc = Bus::read32(vector_addr);
	sh2.in_exception_sequence = false;

	set_pc(new_pc);
	sh2.pipeline_valid = false;
	sh2.load_reg = -1;
	//The vector read is the sequence's, not the next instruction's predecessor's
	sh2.read_this_instr = false;
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
	ss.write(sh2.fetch_cycles);
	ss.write(sh2.pipeline_src_addr);
	ss.write(sh2.pipeline_instruction);
	ss.write(sh2.pipeline_valid);
	ss.write(sh2.in_delay_slot);
	ss.write(sh2.in_nointerrupt_slot);
	ss.write(sh2.request_time);
	ss.write(sh2.read_this_instr);
	ss.write(sh2.wram_read_this_instr);
	ss.write(sh2.port_fetch_stall);
	ss.write(sh2.oram_trail);
	ss.write(sh2.rte_return);
	ss.write(sh2.dma_busy);
	ss.write(sh2.sleeping);
	ss.write(sh2.fetch_address_error);
	ss.write(sh2.fetch_error_pc);
	ss.write(sh2.dram_row);
	ss.write(sh2.load_reg);
	ss.write(sh2.dram_last_access);
	ss.write(sh2.dram_last_end);
	ss.write(sh2.dram_last_was_write);
	ss.write(sh2.mul_issue_free);
	ss.write(sh2.mul_result_ready);
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
	ss.read(sh2.fetch_cycles);
	ss.read(sh2.pipeline_src_addr);
	ss.read(sh2.pipeline_instruction);
	ss.read(sh2.pipeline_valid);
	ss.read(sh2.in_delay_slot);
	ss.read(sh2.in_nointerrupt_slot);
	ss.read(sh2.request_time);
	ss.read(sh2.read_this_instr);
	ss.read(sh2.wram_read_this_instr);
	ss.read(sh2.port_fetch_stall);
	ss.read(sh2.oram_trail);
	ss.read(sh2.rte_return);
	ss.read(sh2.dma_busy);
	ss.read(sh2.sleeping);
	ss.read(sh2.fetch_address_error);
	ss.read(sh2.fetch_error_pc);
	ss.read(sh2.dram_row);
	ss.read(sh2.load_reg);
	ss.read(sh2.dram_last_access);
	ss.read(sh2.dram_last_end);
	ss.read(sh2.dram_last_was_write);
	ss.read(sh2.mul_issue_free);
	ss.read(sh2.mul_result_ready);

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