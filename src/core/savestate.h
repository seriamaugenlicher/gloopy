#pragma once
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

/*
Save state serialization support.

A Snapshot is a flat byte buffer that subsystems write their state into (in a
fixed order defined by System::save_state) and read it back from in the same
order. Values are stored in host byte order; save states are not portable
across architectures, only across runs of the emulator on the same platform.

Section tags (fourcc markers) are written between subsystems so that a
mismatch between save and load order is caught immediately instead of
silently corrupting everything downstream.

All read methods throw std::runtime_error on buffer underrun or tag mismatch.
*/

namespace SaveState
{

constexpr uint32_t fourcc(const char (&s)[5])
{
	return (uint32_t)(uint8_t)s[0] | ((uint32_t)(uint8_t)s[1] << 8) | ((uint32_t)(uint8_t)s[2] << 16) |
		   ((uint32_t)(uint8_t)s[3] << 24);
}

class Snapshot
{
public:
	template <typename T>
	void write(const T& value)
	{
		static_assert(std::is_trivially_copyable<T>::value, "Snapshot::write requires a trivially copyable type");
		write_blob(&value, sizeof(T));
	}

	template <typename T>
	void read(T& value)
	{
		static_assert(std::is_trivially_copyable<T>::value, "Snapshot::read requires a trivially copyable type");
		read_blob(&value, sizeof(T));
	}

	void write_blob(const void* data, size_t size)
	{
		const uint8_t* bytes = (const uint8_t*)data;
		buffer.insert(buffer.end(), bytes, bytes + size);
	}

	void read_blob(void* data, size_t size)
	{
		if (read_pos + size > buffer.size())
		{
			throw std::runtime_error("save state truncated");
		}
		memcpy(data, buffer.data() + read_pos, size);
		read_pos += size;
	}

	void skip(size_t size)
	{
		if (read_pos + size > buffer.size())
		{
			throw std::runtime_error("save state truncated");
		}
		read_pos += size;
	}

	void write_string(const std::string& s)
	{
		uint32_t len = (uint32_t)s.size();
		write(len);
		write_blob(s.data(), len);
	}

	std::string read_string()
	{
		uint32_t len;
		read(len);
		if (len > buffer.size() - read_pos)
		{
			throw std::runtime_error("save state truncated");
		}
		std::string s((const char*)buffer.data() + read_pos, len);
		read_pos += len;
		return s;
	}

	void begin_section(uint32_t tag)
	{
		write(tag);
	}

	void expect_section(uint32_t tag)
	{
		uint32_t actual;
		read(actual);
		if (actual != tag)
		{
			throw std::runtime_error("save state section mismatch");
		}
	}

	//Direct buffer access for frontends that keep states in memory (libretro)
	const uint8_t* data() const
	{
		return buffer.data();
	}

	size_t size() const
	{
		return buffer.size();
	}

	void assign(const void* data, size_t size)
	{
		const uint8_t* bytes = (const uint8_t*)data;
		buffer.assign(bytes, bytes + size);
		read_pos = 0;
	}

	bool save_file(const std::filesystem::path& path)
	{
		std::ofstream file(path, std::ios::binary);
		if (!file.is_open())
		{
			return false;
		}
		file.write((const char*)buffer.data(), buffer.size());
		return file.good();
	}

	bool load_file(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file.is_open())
		{
			return false;
		}
		buffer.assign(std::istreambuf_iterator<char>(file), {});
		read_pos = 0;
		return true;
	}

private:
	std::vector<uint8_t> buffer;
	size_t read_pos = 0;
};

}  // namespace SaveState
