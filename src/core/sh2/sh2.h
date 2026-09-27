#pragma once

#include "core/savestate.h"

namespace SH2
{

void initialize();
void shutdown();
void run();

//Skip provably-idle spin loops (vblank waits). On by default; exposed so it can
//be turned off to isolate it when a game misbehaves.
void set_idle_skip(bool enable);

void save_state(SaveState::Snapshot& ss);
void load_state(SaveState::Snapshot& ss);

}
