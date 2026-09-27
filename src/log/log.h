#pragma once

#include <cassert>

namespace Log
{

enum Level
{
	VERBOSE,
	TRACE,
	DEBUG,
	INFO,
	WARN,
	ERROR,
};

typedef void (*SinkFunc)(Level level, const char *message);

void set_level(Level level);
void set_sink(SinkFunc sink);
void log(Level level, const char *fmt, ...);
void trace(const char *fmt, ...);
void debug(const char *fmt, ...);
void info(const char *fmt, ...);
void warn(const char *fmt, ...);
void error(const char *fmt, ...);

}  // namespace Log

//Hardware the core does not emulate. Debug builds stop here; release builds warn
//once per place per session, so a program that relies on it (homebrew tried in the
//emulator before a console) is told that a console may behave differently
#define LOG_UNEMULATED(...)                  \
	do                                       \
	{                                        \
		assert(0);                           \
		static bool unemulated_reported_;    \
		if (!unemulated_reported_)           \
		{                                    \
			unemulated_reported_ = true;     \
			Log::warn(__VA_ARGS__);          \
		}                                    \
	} while (0)
