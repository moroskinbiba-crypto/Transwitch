# DeskNX library applet (served by the optional HOME backend).
ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO to your devkitPro installation")
endif

LIBNX    := $(DEVKITPRO)/libnx
PORTLIBS := $(DEVKITPRO)/portlibs/switch
# Cross-compiled SDL ports install their .pc files under the Switch portlibs prefix.
export PKG_CONFIG_PATH := $(PORTLIBS)/lib/pkgconfig:$(PKG_CONFIG_PATH)
export PATH := $(PORTLIBS)/bin:$(PATH)
# Query each SDL component separately: a missing optional .pc file must not
# discard linker flags for the components that are installed.
SDL2_LIBS       := $(shell pkg-config --static --libs SDL2 2>/dev/null)
SDL2_IMAGE_LIBS := $(shell pkg-config --static --libs SDL2_image 2>/dev/null)
SDL2_TTF_LIBS   := $(shell pkg-config --static --libs SDL2_ttf 2>/dev/null)
CURL_LIBS      := $(shell pkg-config --static --libs libcurl 2>/dev/null)
SODIUM_LIBS    := $(shell pkg-config --static --libs libsodium 2>/dev/null)
ifeq ($(strip $(SODIUM_LIBS)),)
SODIUM_LIBS := -lsodium
endif
ifeq ($(strip $(SDL2_LIBS)),)
SDL2_LIBS := -lSDL2
endif
ifeq ($(strip $(SDL2_IMAGE_LIBS)),)
SDL2_IMAGE_LIBS := -lSDL2_image
endif
ifeq ($(strip $(SDL2_TTF_LIBS)),)
SDL2_TTF_LIBS := -lSDL2_ttf
endif

include $(LIBNX)/switch_rules

TARGET      := DeskNXMenu
BUILD       := build
SOURCES     := ../../source ./source
INCLUDES    := ../../include ../../projects/common/include
APP_TITLE   := DeskNX
APP_AUTHOR  := DeskNX contributors
APP_VERSION := 0.2.0
TITLE_ID    := 0100000000001000
# TOPDIR is exported by the outer invocation; fall back to CURDIR when this
# Makefile is invoked directly (e.g. `make -C projects/DeskNXMenu` in CI).
APP_JSON    = $(if $(TOPDIR),$(TOPDIR),$(CURDIR))/DeskNXMenu.json

ARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS      := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections $(ARCH) -D__SWITCH__
ifeq ($(STANDALONE_CHECK),1)
CFLAGS      += -DDESKNX_STANDALONE_NATIVE_CHECK
else
CFLAGS      += -DDESKNX_SYSTEM_APPLET
endif
CFLAGS      += $(INCLUDE)
CXXFLAGS    := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions
ASFLAGS     := -g $(ARCH)
LDFLAGS     = -specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,--gc-sections
LIBS        := $(SDL2_IMAGE_LIBS) $(SDL2_TTF_LIBS) $(SDL2_LIBS) $(CURL_LIBS) $(SODIUM_LIBS) -lnx
LIBDIRS     := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export APP_JSON := $(CURDIR)/DeskNXMenu.json
export OUTDIR := $(CURDIR)/../../SdOut/switch/DeskNX/bin/Menu
export VPATH := $(foreach dir,$(SOURCES),$(abspath $(CURDIR)/$(dir)))
export DEPSDIR := $(CURDIR)/$(BUILD)
MENU_CPPFILES := $(filter-out main.cpp,$(notdir $(wildcard ./source/*.cpp)))
COMMON_CPPFILES := $(notdir $(wildcard ../../source/*.cpp))
# source/main.cpp is the full HOME UI: it owns MenuReady, heartbeat, IPC,
# catalogue, input, workspace persistence and game/NRO launch requests.
# The smaller projects/DeskNXMenu/source/main.cpp is a prototype and must not
# be linked as a second entry point or used instead of the system UI.
CPPFILES := $(COMMON_CPPFILES) $(MENU_CPPFILES)
CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_SRC)
export HFILES := $(foreach dir,$(INCLUDES),$(wildcard $(dir)/*.h))
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(abspath $(CURDIR)/$(dir))) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export APP_TITLE APP_AUTHOR APP_VERSION APP_JSON
.PHONY: all clean standalone-check
all: $(BUILD)
$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
# Compile the non-system launch path too. The production HOME applet build
# defines DESKNX_SYSTEM_APPLET and would otherwise leave the standalone branch
# of source/main.cpp unchecked by CI.
standalone-check:
	@mkdir -p build-standalone-check
	@$(MAKE) --no-print-directory -C build-standalone-check -f $(CURDIR)/Makefile STANDALONE_CHECK=1 BUILD=build-standalone-check standalone-compile-check
clean:
	@rm -rf $(BUILD) build-standalone-check $(TARGET).elf $(TARGET).nso $(TARGET).npdm $(TARGET).nsp
	@rm -rf $(CURDIR)/../../SdOut/switch/DeskNX/bin/Menu
else
DEPENDS := $(OFILES:.o=.d)
ifeq ($(STANDALONE_CHECK),1)
.PHONY: standalone-compile-check
standalone-compile-check: $(OFILES)
else
.PHONY: all
all: $(OUTDIR)/main $(OUTDIR)/main.npdm
endif

$(OUTPUT).elf: $(OFILES)

$(OUTDIR)/main: $(OUTPUT).nso | $(OUTDIR)
	@cp -f $< $@
	@echo "DeskNX applet NSO: $@"

$(OUTDIR)/main.npdm: $(APP_JSON) | $(OUTDIR)
	@npdmtool $(APP_JSON) $@
	@echo "DeskNX applet NPDM: $@"

$(OUTDIR):
	@mkdir -p $@

-include $(DEPENDS)
endif
