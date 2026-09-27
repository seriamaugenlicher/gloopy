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

	int fetch_cycles;

	//Instruction-fetch fast path: cached backing pointer and cycle cost for
	//the 4KB page the PC is executing in. Avoids two address translations
	//per instruction. Not serialized; invalidated on initialize/load_state.
	//fetch_cache_page is the page-aligned PC; 1 is the invalid sentinel
	//(never page-aligned).
	uint32_t fetch_cache_page;
	uint8_t* fetch_cache_base;
	int fetch_cache_cycles;
	bool fetch_cache_dram;

	uint32_t pipeline_src_addr;
	uint16_t pipeline_instruction;
	bool pipeline_valid;

	bool in_delay_slot;
	bool in_nointerrupt_slot;

	//When the INTC started presenting the current request (INTERRUPT_LATENCY)
	int64_t request_time;
	//Inside raise_exception: its stack pushes follow their own write rule, and
	//exception_pushed says the SR push is done
	bool in_exception_sequence;
	bool exception_pushed;
	//External-bus code: this / the previous instruction accessed memory over the
	//bus (a delayed branch right after such an access has its extra state free)
	bool read_this_instr;
	bool read_prev_instr;
	//This / the previous instruction read data from work RAM (RTE after one
	//pays no extra state for its own pops)
	bool wram_read_this_instr;
	bool wram_read_prev_instr;

	//Data-access bus time (sh2_bus.cpp). exec_external: the instruction was fetched
	//over the external bus. in_execute: only an instruction's own accesses are
	//charged. Work RAM keeps one DRAM row open until another row or a refresh.
	bool in_execute;
	bool exec_external;
	uint32_t dram_row;
	//Internal-bus code: bus cycles its data accesses took this instruction
	int port_access_cycles;
	//...whether its only accesses were reads on the internal bus (on-chip
	//peripherals, on-chip RAM), and whether such a read waited for the fetch (a
	//load-use stall right after overlaps that wait)
	bool port_internal_read_only;
	bool port_fetch_stall;
	//External-bus code: the cycle of an on-chip RAM access, paid once the next
	//instruction has passed the interrupt check
	int oram_trail;
	//The next instruction is the one an RTE returns to
	bool rte_return;

	//Multiplier: when it takes the next operation, and when MACH/MACL are ready
	int64_t mul_issue_free;
	int64_t mul_result_ready;

	int64_t dram_last_access;     //a refresh since then has closed the row
	int64_t dram_last_end;
	bool dram_last_was_write;

	//Slice start time and budget, so the bus model can tell the time cheaply. Set by
	//run(), not serialized.
	int64_t slice_start_time;
	int32_t slice_entry_cycles;

	//General register the previous instruction loaded from memory, or -1. The
	//next instruction stalls a state if it uses it (load-use interlock).
	int load_reg;

	//A DMA transfer is running or starting: run() gives the DMAC its turns
	bool dma_busy;
	//Bus transfers (data; the fetch is added) the instruction just executed made,
	//for cycle-stealing DMA
	int bus_transfers;

	//SLEEP: the CPU is stopped until an interrupt is accepted
	bool sleeping;
	//Address errors (vector 9): a misaligned data access by the instruction just
	//executed, or a fetch from an odd address waiting to be taken
	bool data_address_error;
	bool fetch_address_error;
	uint32_t fetch_error_pc;

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

//The current CPU time, inside SH2::run()
inline int64_t cpu_now()
{
	return sh2.slice_start_time + (sh2.slice_entry_cycles - sh2.cycles_left);
}

//DRAM refresh: every 244 cycles (the BIOS's RTCOR), 3 states long. It closes the
//open DRAM row and delays only work RAM accesses that meet it (charge_access).
constexpr int DRAM_REFRESH_PERIOD = 244;
//Work RAM's DRAM rows are 512 bytes (measured)
constexpr int DRAM_ROW_SHIFT = 9;
constexpr int DRAM_REFRESH_STATES = 3;

void assert_irq(int vector_id, int prio);
//Push SR and the return address and jump through vector_id (interrupts, TRAPA)
void raise_exception(int vector_id);
//The same with an explicit return address
void raise_exception_to(int vector_id, uint32_t return_pc);
void set_pc(uint32_t new_pc);
void set_sr(uint32_t new_sr);

void add_hook(uint32_t address, HookFunc hook);
void remove_hook(uint32_t address);

}