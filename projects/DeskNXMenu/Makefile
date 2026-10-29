# DeskNX library applet (served by the optional HOME backend).
ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO to your devkitPro installation")
endif

LIBNX    := $(DEVKITPRO)/libnx
PORTLIBS := $(DEVKITPRO)/portlibs/switch
# Cross-compiled SDL ports install their .pc files under the Switch portlibs prefix.
export PKG_CONFIG_PATH := $(PORTLIBS)/lib/pkgconfig:$(PKG_CONFIG_PATH)
export PATH := $(PORTLIBS)/bin:$(PATH)
# devkitA64 ships no sdl2-config; use plain pkg-config for all SDL components.
SDL_PC := SDL2 SDL2_image SDL2_ttf

include $(LIBNX)/switch_rules

TARGET      := DeskNXMenu
BUILD       := build
SOURCES     := ../../source
INCLUDES    := ../../include ../../projects/common/include
APP_TITLE   := DeskNX
APP_AUTHOR  := DeskNX contributors
APP_VERSION := 0.2.0
# TOPDIR is exported by the outer invocation; fall back to CURDIR when this
# Makefile is invoked directly (e.g. `make -C projects/DeskNXMenu` in CI).
APP_JSON    = $(if $(TOPDIR),$(TOPDIR),$(CURDIR))/DeskNXMenu.json

ARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS      := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections $(ARCH) -D__SWITCH__ -DDESKNX_SYSTEM_APPLET
CFLAGS      += $(INCLUDE)
CXXFLAGS    := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions
ASFLAGS     := -g $(ARCH)
LDFLAGS     = -specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,--gc-sections
LIBS        := $(shell pkg-config --static --libs $(SDL_PC)) -lnx
LIBDIRS     := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export APP_JSON := $(CURDIR)/DeskNXMenu.json
export OUTDIR := $(CURDIR)/../../SdOut/switch/DeskNX/bin/Menu
export VPATH := $(foreach dir,$(SOURCES),$(abspath $(CURDIR)/$(dir)))
export DEPSDIR := $(CURDIR)/$(BUILD)
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
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
.PHONY: all clean
all: $(BUILD)
$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nso $(TARGET).npdm $(TARGET).nsp
	@rm -rf $(CURDIR)/../../SdOut/switch/DeskNX/bin/Menu
else
DEPENDS := $(OFILES:.o=.d)
.PHONY: all
all: $(OUTDIR)/main $(OUTDIR)/main.npdm

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
