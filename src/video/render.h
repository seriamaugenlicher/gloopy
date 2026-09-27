#pragma once

namespace Video::Renderer
{

void draw_scanline(int y);

//Clears per-session renderer state; called when the VDP is initialized
void reset();

}