#include "log.h"

#include <cstdarg>
#include <cstdio>

//When a sink is installed (the libretro glue routes messages into the
//frontend's logger), formatted messages go there; otherwise to stdout.
#define PRINTF(x)                             \
	if (level > x) return;                    \
	va_list args;                             \
	va_start(args, fmt);                      \
	if (sink)                                 \
	{                                         \
		char buffer[1024];                    \
		vsnprintf(buffer, sizeof(buffer), fmt, args); \
		sink(x, buffer);                      \
	}                                         \
	else                                      \
	{                                         \
		log_level(x);                         \
		vprintf(fmt, args);                   \
		printf("\n");                         \
	}                                         \
	va_end(args);

namespace Log
{

static Level level = INFO;
static SinkFunc sink = nullptr;

void set_level(Level l)
{
	level = l;
}

void set_sink(SinkFunc s)
{
	sink = s;
}

void log_level(Level l)
{
	switch (l)
	{
	case TRACE:
		printf("[TRACE] ");
		break;
	case DEBUG:
		printf("[DEBUG] ");
		break;
	case INFO:
		printf("[INFO] ");
		break;
	case WARN:
		printf("[WARN] ");
		break;
	case ERROR:
		printf("[ERROR] ");
		break;
	default:
		break;
	}
}

void trace(const char *fmt, ...)
{
	PRINTF(TRACE);
}

void debug(const char *fmt, ...)
{
	PRINTF(DEBUG);
}

void info(const char *fmt, ...)
{
	PRINTF(INFO);
}

void warn(const char *fmt, ...)
{
	PRINTF(WARN);
}

void error(const char *fmt, ...)
{
	PRINTF(ERROR);
}

}  // namespace Log
