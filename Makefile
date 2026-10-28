# DeskNX Nintendo Switch homebrew
ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO is not set. Install devkitPro and switch-dev.")
endif

# Explicit paths also work in the devkitPro Docker image where profile
# environment scripts may not be sourced by the Actions runner.
LIBNX    := $(DEVKITPRO)/libnx
PORTLIBS := $(DEVKITPRO)/portlibs/switch

include $(LIBNX)/switch_rules

TARGET      := DeskNX
BUILD       := build
SOURCES     := source
INCLUDES    := include projects/common/include
# ROMFS intentionally disabled: do not package legacy translation assets.

APP_TITLE   := DeskNX
APP_AUTHOR  := DeskNX contributors
APP_VERSION := 0.1.0

ARCH        := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS      := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections $(ARCH) -D__SWITCH__
CFLAGS      += $(INCLUDE)
CXXFLAGS    := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions
ASFLAGS     := -g $(ARCH)
LDFLAGS     = -specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,--gc-sections

LIBS        := $(shell sdl2-config --libs)
LIBDIRS     := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_SRC)
export HFILES := $(foreach dir,$(INCLUDES),$(wildcard $(dir)/*.h))
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export BUILD_EXEFS_SRC := $(TOPDIR)/exefs_src
export APP_TITLE APP_AUTHOR APP_VERSION
export ROMFS
ifneq ($(strip $(ROMFS)),)
export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif
export APP_ICON := $(LIBNX)/default_icon.jpg
.PHONY: all clean menu-applet system-backend system-package package-system
all: $(BUILD)

# Build the library applet served by the optional HOME backend.
menu-applet:
	@$(MAKE) --no-print-directory -C projects/DeskNXMenu

# Requires a matching Atmosphere/libstratosphere build; never part of the
# normal NRO build because a bad qlaunch override can prevent HOME from opening.
system-backend:
	@$(MAKE) --no-print-directory -C projects/sSystem

system-package: all menu-applet system-backend
	@mkdir -p SdOut/switch/DeskNX
	@cp -f DeskNX.nro SdOut/switch/DeskNX/DeskNX.nro
	@cp -f README.md SdOut/README-DeskNX.md
	@echo "System SD layout staged in SdOut/ (not installed to the SD card)."

package-system: system-package
	@python3 -c 'import pathlib,zipfile; root=pathlib.Path("SdOut"); z=zipfile.ZipFile("DeskNX-system-sdcard.zip","w",zipfile.ZIP_DEFLATED); [z.write(p,p.relative_to(root)) for p in root.rglob("*") if p.is_file()]; z.close()'
	@echo "Created DeskNX-system-sdcard.zip"
$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -rf $(BUILD) $(TARGET).nro $(TARGET).nso $(TARGET).elf $(TARGET).nacp $(TARGET).pfs0
	@$(MAKE) --no-print-directory -C projects/DeskNXMenu clean 2>/dev/null || true
	@$(MAKE) --no-print-directory -C projects/sSystem clean 2>/dev/null || true
	@rm -rf SdOut DeskNX-system-sdcard.zip
else
DEPENDS := $(OFILES:.o=.d)
.PHONY: all
all: $(OUTPUT).nro
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif
