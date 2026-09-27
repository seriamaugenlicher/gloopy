#include "printer/printer.h"

#include <core/sh2/peripherals/sh2_pfc.h>
#include <core/sh2/sh2_bus.h>
#include <core/sh2/sh2_local.h>
#include <log/log.h>
#include <imgwriter/imgwriter.h>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
namespace imagew = ImageWriter;

namespace Printer
{

constexpr static uint32_t ADDR_MOTOR_MOVE = 0x00001B76;
constexpr static uint32_t ADDR_MOTOR_MOVE_RETURN = 0x000015FA;
constexpr static uint32_t ADDR_PRINT = 0x000006D4;
constexpr static uint32_t ADDR_PRINT_RETURN = 0x00000FD2;

//Status codes the BIOS print routine returns
constexpr static int PRINT_STATUS_SUCCESS = 0;
constexpr static int PRINT_STATUS_GENERAL_FAILURE = 1;
constexpr static int PRINT_STATUS_NO_SEAL_CART = 2;
constexpr static int PRINT_STATUS_CANCELLED = 3;
constexpr static int PRINT_STATUS_PAPER_JAM = 4;
constexpr static int PRINT_STATUS_OVERHEAT = 5;

static fs::path output_dir;
static int output_type;

using namespace SH2;

template <typename T>
std::vector<T> double_pixel_data(std::vector<T> data, uint32_t width, uint32_t height)
{
	std::vector<T> data_doubled(width * height * 4);
	for (int y = 0; y < height * 2; y++)
	{
		for (int x = 0; x < width * 2; x++)
		{
			data_doubled[y * (width * 2) + x] = data[(y / 2) * width + (x / 2)];
		}
	}
	return data_doubled;
}

static bool motor_move_hook(uint32_t addr)
{
	//Hook slow moving printer function and skip it for faster boot
	if (addr != ADDR_MOTOR_MOVE) return false;

	//Go to end of function (rts / _nop) skipping this instruction.
	//Routine and expected, so debug rather than info: the log is where someone
	//goes to find out what went wrong, and this never has.
	Log::debug("[Printer] skipping motor move");
	sh2.pc = ADDR_MOTOR_MOVE_RETURN;
	sh2.pipeline_valid = false;
	return true;
}

static bool print_hook(uint32_t addr)
{
	//Hook the BIOS print function entry point
	if (addr != ADDR_PRINT) return false;

	uint32_t sp = sh2.gpr[15];
	uint32_t p1_data = Bus::read32(sh2.gpr[4]);
	uint32_t p2_palette = Bus::read32(sh2.gpr[5]);
	uint32_t p3_dims = Bus::read32(sh2.gpr[6]);
	uint32_t p4_unk = sh2.gpr[7];
	uint32_t p5_unk = Bus::read32(sp);
	uint32_t p6_format = Bus::read8(Bus::read32(sp + 4));
	uint32_t p7_unk = Bus::read32(sp + 8);
	uint32_t p8_first = Bus::read32(sp + 12);
	Log::debug(
		"[Printer] data=%08X, palette=%08X, dims=%08X, unkp4=%08X, unkp5=%08X, format=%02X, unkp7=%08X, first=%d",
		p1_data, p2_palette, p3_dims, p4_unk, p5_unk, p6_format, p7_unk, p8_first
	);

	//What a console does with arguments the BIOS's own wrappers never pass: with p5
	//0 (the wrappers pass 0xF1) it reports no seal cartridge
	int status = -1;
	if (p5_unk == 0)
	{
		Log::warn("[Printer] print called with p5 = 0: a console reports no seal cartridge (the BIOS passes 0xF1)");
		status = PRINT_STATUS_NO_SEAL_CART;
	}
	//The paper feed's motor steps are paced through PA13 set to DREQ0; with the pin
	//given to IRQ1 a console's feed times out (status 4)
	else if (!SH2::OCPM::PFC::pa13_is_dreq0())
	{
		Log::warn("[Printer] print with PA13 not set to DREQ0 (PACR1): a console's paper feed times out");
		status = PRINT_STATUS_PAPER_JAM;
	}
	else if (output_dir.empty())
	{
		//Nowhere to save
		status = PRINT_STATUS_NO_SEAL_CART;
	}
	if (status >= 0)
	{
		//Return the status, go to end of function (rts / _mov.l) after this instruction
		sh2.gpr[0] = status;
		sh2.pc = ADDR_PRINT_RETURN;
		sh2.pipeline_valid = false;
		return false;
	}

	bool print_success = false;

	// Dump the data to be printed
	uint32_t width = p3_dims & 0xFFFF;
	uint32_t height = p3_dims >> 16;

	int pixel_double = p6_format >> 4;
	int pixel_format = p6_format & 15;

	//TODO: is there more complex logic to this?
	height = std::min(height, (uint32_t)(pixel_double == 1 ? 112 : 224));

	Log::info("[Printer] size=%dx%d, pixel_format=%d, pixel_double=%d", width, height, pixel_format, pixel_double);

	if ((pixel_double == 0 || pixel_double == 1) && (pixel_format == 1 || pixel_format == 3))
	{
		fs::path print_name = imagew::make_unique_name("loopyseal_");
		print_name += imagew::image_extension(output_type);
		fs::path print_path = fs::absolute(output_dir) / print_name;

		if (pixel_format == 3)
		{
			std::vector<uint8_t> data(width * height);
			uint16_t palette[256];

			for (int i = 0; i < (width * height); i++)
			{
				data[i] = Bus::read8(p1_data + i);
			}
			for (int p = 0; p < 256; p++)
			{
				palette[p] = Bus::read16(p2_palette + (p * 2));
			}

			if (pixel_double == 1)
			{
				std::vector<uint8_t> data_doubled = double_pixel_data<uint8_t>(data, width, height);
				print_success = imagew::save_image_8bpp(
					output_type, print_path, width * 2, height * 2, &data_doubled[0], 256, palette
				);
			}
			else
			{
				print_success = imagew::save_image_8bpp(output_type, print_path, width, height, &data[0], 256, palette);
			}
		}
		if (pixel_format == 1)
		{
			std::vector<uint16_t> data(width * height);

			for (int i = 0; i < (width * height); i++)
			{
				data[i] = Bus::read16(p1_data + (i * 2));
			}

			if (pixel_double == 1)
			{
				std::vector<uint16_t> data_doubled = double_pixel_data<uint16_t>(data, width, height);
				print_success =
					imagew::save_image_16bpp(output_type, print_path, width * 2, height * 2, &data_doubled[0]);
			}
			else
			{
				print_success = imagew::save_image_16bpp(output_type, print_path, width, height, &data[0]);
			}
		}

		if (print_success)
		{
			Log::info("[Printer] saved print to %s", print_name.string().c_str());
		}
		else
		{
			Log::warn("[Printer] failed to open %s", print_name.string().c_str());
		}
	}
	else
	{
		Log::warn("[Printer] unknown mode, aborting");
	}

	if (print_success)
	{
		//Let the real BIOS routine run on; print_return_hook reports success as it returns
		return false;
	}

	//Return failure status, go to end of function (rts / _mov.l) after this instruction
	sh2.gpr[0] = PRINT_STATUS_GENERAL_FAILURE;
	sh2.pc = ADDR_PRINT_RETURN;
	sh2.pipeline_valid = false;
	return false;
}

static bool print_return_hook(uint32_t addr)
{
	//Hook just before the BIOS print function exit point
	if (addr != (ADDR_PRINT_RETURN - 2)) return false;

	//Return success status, continue execution
	sh2.gpr[0] = PRINT_STATUS_SUCCESS;
	return false;
}

void set_output_directory(const fs::path& dir)
{
	output_dir = dir;

	//A print can fail hours into a game, long after anyone would connect it to
	//setup, so say up front where prints will go and whether that will work. The
	//directory is normally the frontend's save directory and already exists;
	//create it if not, and let a failure here surface now rather than at print
	//time. An empty output_dir means the printer is switched off.
	if (output_dir.empty())
	{
		Log::info("[Printer] switched off; prints report no seal cartridge");
	}
	else
	{
		std::error_code ec;
		fs::create_directories(output_dir, ec);
		if (!fs::is_directory(output_dir, ec))
		{
			Log::warn("[Printer] cannot use %s as the print directory; prints will fail",
					  fs::absolute(output_dir).string().c_str());
		}
		else
		{
			Log::info("[Printer] prints will be saved to %s", fs::absolute(output_dir).string().c_str());
		}
	}
}

void initialize(Config::SystemInfo& config)
{
	output_type = config.emulator.printer_image_type;
	set_output_directory(config.emulator.image_save_directory);

	SH2::add_hook(ADDR_MOTOR_MOVE, &motor_move_hook);
	SH2::add_hook(ADDR_PRINT, &print_hook);
	SH2::add_hook(ADDR_PRINT_RETURN - 2, &print_return_hook);
	Log::debug("[Printer] registered hooks for print and motor-move BIOS calls");
}

void set_image_type(int image_type)
{
	output_type = image_type;
}

void shutdown()
{
	output_dir.clear();

	SH2::remove_hook(ADDR_MOTOR_MOVE);
	SH2::remove_hook(ADDR_PRINT);
	SH2::remove_hook(ADDR_PRINT_RETURN - 2);
	Log::debug("[Printer] unregistered hooks");
}

}  // namespace Printer
