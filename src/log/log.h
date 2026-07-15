#pragma once

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