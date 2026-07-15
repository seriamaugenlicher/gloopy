#pragma once
#include <cstdint>
#include <unordered_map>

namespace SH2
{

typedef bool (*HookFunc)(uint32_t);

struct CPU
{
	uint32_t gpr[16];
	uint32_t pc;
	uint32_t pr;
	uint32_t macl, mach;
	uint32_t gbr, vbr;
	uint32_t sr;

	int32_t cycles_left;

	int pending_exception_prio;
	int pending_exception_vector;

	uint8_t** pagetable;

	std::unordered_map<uint32_t, HookFunc> hooks;

	//Address range covered by hooks, so the hot loop can skip the map lookup
	//with two compares (hooks only exist at a few BIOS addresses). When no
	//hooks are registered, min > max and nothing matches.
	uint32_t hook_min;
	uint32_t hook_max;

	bool fetch_done;
	int fetch_cycles;

	//Instruction-fetch fast path: cached backing pointer and cycle cost for
	//the 4KB page the PC is executing in. Avoids two address translations
	//per instruction. Not serialized; invalidated on initialize/load_state.
	//fetch_cache_page is the page-aligned PC; 1 is the invalid sentinel
	//(never page-aligned).
	uint32_t fetch_cache_page;
	uint8_t* fetch_cache_base;
	int fetch_cache_cycles;

	uint32_t pipeline_src_addr;
	uint16_t pipeline_instruction;
	bool pipeline_valid;

	bool in_delay_slot;
	bool in_nointerrupt_slot;

	//Idle-loop skip. Games spin-wait for vblank by polling a VDP register (the
	//BIOS wait routine at 0x6A76 accounts for >90% of all instructions executed
	//in most titles). All emulated state advances only inside scheduler events,
	//so a loop that returns to its own head having written nothing, read nothing
	//time-dependent, and left every register identical cannot possibly observe a
	//change before the next event - the rest of the timeslice can be consumed at
	//once. These fields are pure detection state, rebuilt from scratch each
	//timeslice, and are deliberately not serialized.
	uint32_t idle_prev_addr;
	uint32_t idle_head;
	uint32_t idle_snapshot[22];
	bool idle_armed;
	bool idle_snapshot_valid;
	bool idle_wrote_mem;
	bool idle_unsafe_read;
};

extern CPU sh2;

void assert_irq(int vector_id, int prio);
void set_pc(uint32_t new_pc);
void set_sr(uint32_t new_sr);

void add_hook(uint32_t address, HookFunc hook);
void remove_hook(uint32_t address);

}