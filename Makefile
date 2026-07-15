DEBUG = 0

CORE_DIR := .
TARGET_NAME := gloopy

# Every build lands in dist/. Keeping cores out of the source root means there is
# one place to look for the thing you just built, and no stale copy lying beside
# the sources to be picked up by mistake.
# Override with OUTDIR=. if something expects the core in the root (libretro-super).
OUTDIR ?= dist

# system platform
ifeq ($(platform),)
   platform = unix
   ifeq ($(shell uname -s),)
      platform = win
   else ifneq ($(findstring MINGW,$(shell uname -s)),)
      platform = win
   else ifneq ($(findstring MSYS,$(shell uname -s)),)
      platform = win
   else ifneq ($(findstring Darwin,$(shell uname -s)),)
      platform = osx
   endif
endif

# GIT HASH
GIT_VERSION := " $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
   CXXFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

# Unix
ifneq (,$(findstring unix,$(platform)))
   TARGET := $(OUTDIR)/$(TARGET_NAME)_libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T

# OS X
else ifneq (,$(findstring osx,$(platform)))
   TARGET := $(OUTDIR)/$(TARGET_NAME)_libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   MINVERSION := -mmacosx-version-min=10.15
   CFLAGS += $(MINVERSION)
   CXXFLAGS += $(MINVERSION)
   LDFLAGS += $(MINVERSION)
   ifeq ($(CROSS_COMPILE),1)
      TARGET_RULE = -target $(LIBRETRO_APPLE_PLATFORM) -isysroot $(LIBRETRO_APPLE_ISYSROOT)
      CFLAGS += $(TARGET_RULE)
      CXXFLAGS += $(TARGET_RULE)
      LDFLAGS += $(TARGET_RULE)
   endif

# Android (cross-compile with the NDK's clang)
# platform=android_arm64 or android_arm; set ANDROID_NDK to the NDK root, and
# NDK_HOST to the prebuilt host tag if you are not building from Windows.
else ifneq (,$(findstring android,$(platform)))
   TARGET := $(OUTDIR)/$(TARGET_NAME)_libretro_android.so
   fpic := -fPIC
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   ifeq ($(ANDROID_NDK),)
      $(error Set ANDROID_NDK to the NDK root, e.g. make platform=android_arm64 ANDROID_NDK=/path/to/android-ndk-r27c)
   endif
   NDK_HOST ?= windows-x86_64
   NDK_LLVM_BIN := $(ANDROID_NDK)/toolchains/llvm/prebuilt/$(NDK_HOST)/bin
   CC := $(NDK_LLVM_BIN)/clang
   CXX := $(NDK_LLVM_BIN)/clang++
   ifneq (,$(findstring android_arm64,$(platform)))
      ANDROID_TARGET := aarch64-linux-android21
   else
      ANDROID_TARGET := armv7a-linux-androideabi21
      FLAGS += -mfpu=neon
   endif
   FLAGS += --target=$(ANDROID_TARGET)
   LDFLAGS += --target=$(ANDROID_TARGET) -static-libstdc++

# Windows (native MSYS2/MinGW or cross-compile)
else ifneq (,$(findstring win,$(platform)))
   TARGET := $(OUTDIR)/$(TARGET_NAME)_libretro.dll
   SHARED := -shared -static-libgcc -static-libstdc++ -Wl,--no-undefined -Wl,--version-script=link.T
   ifneq ($(findstring MINGW,$(shell uname -s))$(findstring MSYS,$(shell uname -s)),)
      CC ?= gcc
      CXX ?= g++
   else
      CC = x86_64-w64-mingw32-gcc
      CXX = x86_64-w64-mingw32-g++
   endif

# Generic fallback (static library, e.g. for consoles; untested)
else
   TARGET := $(OUTDIR)/$(TARGET_NAME)_libretro_$(platform).a
   STATIC_LINKING = 1
endif

# The emulation core marks unimplemented hardware paths with assert(0);
# upstream's shipped Release builds compile them out with NDEBUG, and a
# firing assert would abort the whole frontend process, so release builds
# of the core must do the same. DEBUG=1 keeps the asserts live.
# OPTIMIZE / OPTIMIZE_LD may be overridden from the command line.
ifeq ($(DEBUG), 1)
   OPTIMIZE ?= -O0 -g
else
   OPTIMIZE ?= -O2 -DNDEBUG
endif
FLAGS += $(OPTIMIZE)
LDFLAGS += $(OPTIMIZE_LD)

include Makefile.common

# Objects go in a per-build directory. Every target used to compile into the same
# paths next to the sources, so building for a second platform silently overwrote
# the first one's objects with foreign-architecture ones - the next native build
# then failed with "file format not recognized", and 'clean' before a cross-build
# also wiped the native binary. Keeping them apart means builds no longer collide
# and switching between them needs no clean.
#
# Cross-builds that share a platform must set BUILD_TAG to stay apart: both the
# aarch64 and x86_64 Linux cores are platform=unix, so they would otherwise land
# in the same directory. e.g. make platform=unix BUILD_TAG=aarch64 CC="..." ...
OBJDIR := obj/$(platform)$(if $(BUILD_TAG),-$(BUILD_TAG),)
OBJECTS := $(addprefix $(OBJDIR)/,$(SOURCES_CXX:.cpp=.o))

FLAGS += $(INCFLAGS) -D__LIBRETRO__ $(fpic)
CXXFLAGS += $(FLAGS) -std=c++17

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(OUTDIR)
ifeq ($(STATIC_LINKING),1)
	$(AR) rcs $@ $(OBJECTS)
else
	$(CXX) $(fpic) $(SHARED) -o $@ $(OBJECTS) $(LDFLAGS)
endif

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Only this platform's objects and target; other platforms' builds are untouched
clean:
	rm -rf $(OBJDIR)
	rm -f $(TARGET)

# Everything, for all platforms
clean-all:
	rm -rf obj
	rm -f $(OUTDIR)/$(TARGET_NAME)_libretro*

.PHONY: clean clean-all all
