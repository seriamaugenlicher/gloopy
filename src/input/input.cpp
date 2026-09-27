#include "input/input.h"

#include <core/loopy_io.h>

namespace Input
{

void initialize()
{
	//Indicate the gamepad is connected
	LoopyIO::set_controller_plugged(true, false);
}

void shutdown()
{
	//nop
}

}  // namespace Input
