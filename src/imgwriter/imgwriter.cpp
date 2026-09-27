#include "imgwriter.h"

#include "png.h"

#include <cstring>
#include <ctime>
#include <fstream>
#include <vector>

namespace ImageWriter
{

fs::path image_extension(int image_type)
{
	return image_type == IMAGE_TYPE_BMP ? fs::path{".bmp"} : fs::path{".png"};
}

fs::path make_unique_name(std::string prefix)
{
	static unsigned int unique_number = 1;

	std::time_t timestamp = std::time(nullptr);
	char timestamp_buffer[20];
	strftime(timestamp_buffer, sizeof(timestamp_buffer), "%Y%m%d_%H%M%S", std::localtime(&timestamp));

	return prefix + timestamp_buffer + "_" + std::to_string(unique_number++);
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

static bool save_image_32bpp(int image_type, fs::path path, uint32_t width, uint32_t height, uint32_t data[])
{
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

bool save_image_16bpp(int image_type, fs::path path, uint32_t width, uint32_t height, uint16_t data[])
{
	unsigned int num_pixels = width * height;
	std::vector<uint32_t> data_argb(num_pixels);

	//Seals are always opaque
	for (unsigned int i = 0; i < num_pixels; i++)
	{
		data_argb[i] = color_16bpp_to_argb(data[i] | 0x8000);
	}

	return save_image_32bpp(image_type, path, width, height, data_argb.data());
}

bool save_image_8bpp(
	int image_type, fs::path path, uint32_t width, uint32_t height, uint8_t data[], uint32_t num_colors,
	uint16_t palette[]
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

	return save_image_16bpp(image_type, path, width, height, data_16bpp.data());
}

}  // namespace ImageWriter
