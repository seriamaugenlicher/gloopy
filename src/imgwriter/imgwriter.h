#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

/*
Dependency-free image writer for printed seals, replacing the standalone
frontend's SDL_image-based one. Writes BMP or PNG.
*/

namespace ImageWriter
{

const int IMAGE_TYPE_BMP = 1;
const int IMAGE_TYPE_PNG = 2;

fs::path image_extension(int image_type);
fs::path make_unique_name(std::string prefix);

bool save_image_16bpp(int image_type, fs::path path, uint32_t width, uint32_t height, uint16_t data[]);
bool save_image_8bpp(
	int image_type, fs::path path, uint32_t width, uint32_t height, uint8_t data[], uint32_t num_colors,
	uint16_t palette[]
);

}  // namespace ImageWriter
