# DeskNX library applet (runs in the HOME daemon's served applet slot).
# This target produces main + main.npdm under SdOut/switch/DeskNX/bin/Menu.
ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO to your devkitPro installation")
endif

include $(DEVKITPRO)/libnx/switch_rules

TARGET      := DeskNXMenu
BUILD       := build
SOURCES     := ../../source
INCLUDES    := ../../projects/common/include
APP_TITLE   := DeskNX
APP_AUTHOR  := DeskNX contributors
APP_VERSION := 0.2.0
APP_JSON    := DeskNXMenu.json

ARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS      := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections $(ARCH) -D__SWITCH__ -DDESKNX_SYSTEM_APPLET
CXXFLAGS    := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions
ASFLAGS     := -g $(ARCH)
LDFLAGS     = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,--gc-sections
LIBS        := $(shell sdl2-config --libs) -lnx
LIBDIRS     := $(PORTLIBS) $(LIBNX)
OUTDIR      := ../../SdOut/switch/DeskNX/bin/Menu

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
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
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(abspath $(CURDIR)/$(dir))) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export APP_TITLE APP_AUTHOR APP_VERSION APP_JSON
.PHONY: all clean
all: $(OUTDIR)/main $(OUTDIR)/main.npdm

$(OUTDIR)/main: $(OUTPUT).nso | $(OUTDIR)
	@cp -f $< $@
	@echo "DeskNX applet NSO: $@"

$(OUTDIR)/main.npdm: $(APP_JSON) | $(OUTDIR)
	@npdmtool $(APP_JSON) $@
	@echo "DeskNX applet NPDM: $@"

$(OUTDIR):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nso $(TARGET).npdm $(TARGET).nsp
else
DEPENDS := $(OFILES:.o=.d)
.PHONY: all
all: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif
