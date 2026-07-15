#include "imgwriter.h"

#include "png.h"

#include <log/log.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <vector>

namespace ImageWriter
{

const double PRINT_ASPECT_CORRECTION_PRESCALE = 4;

int parse_image_type(std::string type, int default_)
{
	std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return std::tolower(c); });
	if (type == "bmp" || type == ".bmp" || type == "bitmap")
	{
		return IMAGE_TYPE_BMP;
	}
	if (type == "jpg" || type == "jpeg" || type == ".jpg" || type == ".jpeg")
	{
		return IMAGE_TYPE_JPG;
	}
	if (type == "png" || type == ".png")
	{
		return IMAGE_TYPE_PNG;
	}
	return default_;
}

fs::path image_extension(int image_type)
{
	//JPEG is not supported and falls back to PNG, so the extension always matches
	//what is actually written
	return image_type == IMAGE_TYPE_BMP ? fs::path{".bmp"} : fs::path{".png"};
}

fs::path make_unique_name(std::string prefix, std::string suffix)
{
	static unsigned int unique_number = 1;

	std::time_t timestamp = std::time(nullptr);
	char timestamp_buffer[20];
	strftime(timestamp_buffer, sizeof(timestamp_buffer), "%Y%m%d_%H%M%S", std::localtime(&timestamp));

	return prefix + timestamp_buffer + "_" + std::to_string(unique_number++) + suffix;
}

//Write a 24-bit uncompressed BMP from ARGB8888 pixels (alpha dropped, as the
//original writer did for BMP output)
static bool save_bmp_24(fs::path path, uint32_t width, uint32_t height, const uint32_t* data)
{
	uint32_t row_bytes = (width * 3 + 3) & ~3u;
	uint32_t image_size = row_bytes * height;
	uint32_t file_size = 14 + 40 + image_size;

	uint8_t header[54] = {};
	header[0] = 'B';
	header[1] = 'M';
	memcpy(header + 2, &file_size, 4);
	uint32_t data_offset = 54;
	memcpy(header + 10, &data_offset, 4);
	uint32_t info_size = 40;
	memcpy(header + 14, &info_size, 4);
	int32_t w = (int32_t)width, h = (int32_t)height;
	memcpy(header + 18, &w, 4);
	memcpy(header + 22, &h, 4);
	uint16_t planes = 1, bpp = 24;
	memcpy(header + 26, &planes, 2);
	memcpy(header + 28, &bpp, 2);
	memcpy(header + 34, &image_size, 4);

	std::ofstream file(path, std::ios::binary);
	if (!file.is_open())
	{
		return false;
	}
	file.write((const char*)header, sizeof(header));

	std::vector<uint8_t> row(row_bytes, 0);
	for (int32_t y = height - 1; y >= 0; y--)
	{
		const uint32_t* src = data + (size_t)y * width;
		for (uint32_t x = 0; x < width; x++)
		{
			uint32_t px = src[x];
			row[x * 3 + 0] = px & 0xFF;			//B
			row[x * 3 + 1] = (px >> 8) & 0xFF;	//G
			row[x * 3 + 2] = (px >> 16) & 0xFF; //R
		}
		file.write((const char*)row.data(), row_bytes);
	}
	return file.good();
}

//Nearest-neighbor resize, replacing SDL_BlitScaled in the original
static std::vector<uint32_t> resize_nearest(
	const uint32_t* data, uint32_t width, uint32_t height, uint32_t new_width, uint32_t new_height
)
{
	std::vector<uint32_t> out((size_t)new_width * new_height);
	for (uint32_t y = 0; y < new_height; y++)
	{
		uint32_t src_y = (uint32_t)((uint64_t)y * height / new_height);
		for (uint32_t x = 0; x < new_width; x++)
		{
			uint32_t src_x = (uint32_t)((uint64_t)x * width / new_width);
			out[(size_t)y * new_width + x] = data[(size_t)src_y * width + src_x];
		}
	}
	return out;
}

bool save_image_32bpp(
	int image_type, fs::path path, uint32_t width, uint32_t height, uint32_t data[], bool _transparent,
	double correct_aspect
)
{
	if (correct_aspect > 0)
	{
		uint32_t scaled_height = (uint32_t)(height * PRINT_ASPECT_CORRECTION_PRESCALE);
		uint32_t scaled_width = (uint32_t)(scaled_height * correct_aspect);
		std::vector<uint32_t> scaled = resize_nearest(data, width, height, scaled_width, scaled_height);
		Log::debug("Rescaling image from %dx%d -> %dx%d", width, height, scaled_width, scaled_height);
		return image_type == IMAGE_TYPE_BMP ? save_bmp_24(path, scaled_width, scaled_height, scaled.data())
											: save_png_24(path, scaled_width, scaled_height, scaled.data());
	}

	return image_type == IMAGE_TYPE_BMP ? save_bmp_24(path, width, height, data)
										: save_png_24(path, width, height, data);
}

static inline uint32_t color_16bpp_to_argb(uint16_t c)
{
	uint8_t r = ((c >> 10) & 31) * 255 / 31;
	uint8_t g = ((c >> 5) & 31) * 255 / 31;
	uint8_t b = (c & 31) * 255 / 31;
	uint8_t a = (c >> 15) * 255;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

bool save_image_16bpp(
	int image_type, fs::path path, uint32_t width, uint32_t height, uint16_t data[], bool transparent,
	double correct_aspect
)
{
	unsigned int num_pixels = width * height;
	std::vector<uint32_t> data_argb(num_pixels);

	uint16_t alpha_set = transparent ? 0 : 0x8000;

	for (unsigned int i = 0; i < num_pixels; i++)
	{
		data_argb[i] = color_16bpp_to_argb(data[i] | alpha_set);
	}

	return save_image_32bpp(image_type, path, width, height, data_argb.data(), transparent, correct_aspect);
}

bool save_image_8bpp(
	int image_type, fs::path path, uint32_t width, uint32_t height, uint8_t data[], uint32_t num_colors,
	uint16_t palette[], bool transparent, double correct_aspect
)
{
	unsigned int num_pixels = width * height;
	std::vector<uint16_t> data_16bpp(num_pixels);

	for (unsigned int i = 0; i < num_pixels; i++)
	{
		uint8_t pixel = data[i];
		if (pixel >= num_colors) pixel = num_colors - 1;
		data_16bpp[i] = palette[pixel];
	}

	return save_image_16bpp(image_type, path, width, height, data_16bpp.data(), transparent, correct_aspect);
}

}  // namespace ImageWriter
