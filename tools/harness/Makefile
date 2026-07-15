## Test harness for the Gloopy core. See README.md in this directory.
##
## Builds against the core's copy of libretro.h. Windows only - it loads the
## core with LoadLibrary.

CXX ?= g++
CORE_DIR := ../..

CXXFLAGS += -std=c++17 -O2 -I$(CORE_DIR)/libretro-common/include

harness: harness.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f harness harness.exe

.PHONY: clean
