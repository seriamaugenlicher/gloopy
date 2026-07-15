LOCAL_PATH := $(call my-dir)

# The libretro Android build-bot drives ndk-build here (it does not use the desktop
# Makefile). CORE_DIR points back at the repo root so the single source-of-truth file
# list in Makefile.common can be reused verbatim — no second copy to drift out of sync.
CORE_DIR := $(abspath $(LOCAL_PATH)/..)

include $(CLEAR_VARS)
include $(CORE_DIR)/Makefile.common

LOCAL_MODULE       := retro
LOCAL_SRC_FILES    := $(SOURCES_CXX)
LOCAL_C_INCLUDES   := $(CORE_DIR)/src $(CORE_DIR)/libretro-common/include
# -DNDEBUG is load-bearing: the core marks unimplemented hardware with assert(0), which
# would abort the whole app; it must be compiled out of release builds (see Makefile).
LOCAL_CXXFLAGS     := -O2 -DNDEBUG -D__LIBRETRO__ -std=c++17
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDFLAGS      := -Wl,--version-script=$(CORE_DIR)/link.T

include $(BUILD_SHARED_LIBRARY)
