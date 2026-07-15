#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace ImageWriter
{

//Write a truecolour PNG from ARGB8888 pixels. Alpha is dropped: the printer never
//asks for transparency, and a seal is a physical sticker.
bool save_png_24(const fs::path& path, uint32_t width, uint32_t height, const uint32_t* argb);

}  //namespace ImageWriter
