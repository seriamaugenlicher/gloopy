#pragma once
#include <cstdint>

namespace SH2::Interpreter
{

void run(uint16_t instr, uint32_t src_addr);

//Invalid instructions executed since content was loaded, and where the first one was.
//The libretro layer resets them at load and dumps the machine state on the first.
extern uint32_t unrecognized_count;
extern uint32_t unrecognized_first_pc;

}