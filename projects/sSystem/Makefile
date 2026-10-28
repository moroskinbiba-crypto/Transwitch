# DeskNX system applet (qlaunch replacement) for Atmosphere.
# This target is intentionally separate from the safe NRO build. It requires
# libstratosphere + AtmosphereLibs built for the exact target firmware.
ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO to your devkitPro installation")
endif

TARGET   := DeskNXSystem
BUILD    := build
SOURCES  := source
INCLUDES := include ../../projects/common/include

# Override when using a separately-installed libnx master build.
LIBNX ?= $(DEVKITPRO)/libnx
ATMOSPHERE_DIR ?= /opt/atmosphere
export ATMOSPHERE_LIBRARIES_DIR := $(ATMOSPHERE_DIR)/libraries
STRAT_SPECS := $(ATMOSPHERE_LIBRARIES_DIR)/libstratosphere/stratosphere.specs

OUTDIR ?= ../../SdOut/atmosphere/contents/0100000000001000/exefs
SYSTEM_JSON ?= $(CURDIR)/DeskNXSystem.json

include $(DEVKITPRO)/libnx/switch_rules

APP_TITLE   := DeskNXSystem
APP_AUTHOR  := DeskNX contributors
APP_VERSION := 0.2.0

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
ATMOSPHERE_DEFINES := -DATMOSPHERE -DATMOSPHERE_ARCH_ARM64 -DATMOSPHERE_ARCH_ARM_V8A \
                      -DATMOSPHERE_BOARD_NINTENDO_NX -DATMOSPHERE_OS_HORIZON -D__SWITCH__ \
                      -DATMOSPHERE_IS_STRATOSPHERE -D_GNU_SOURCE

CFLAGS := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections -fno-strict-aliasing \
          -fwrapv -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector \
          $(ARCH) $(ATMOSPHERE_DEFINES) $(INCLUDE) \
          -I$(LIBNX)/include -I$(DEVKITPRO)/AtmosphereLibs/include \
          -DSL_VERSION=\\\"$(APP_VERSION)\\\"
CXXFLAGS := $(CFLAGS) -std=gnu++23 -fno-rtti -fno-exceptions -Wno-invalid-offset
ASFLAGS := -g $(ARCH)

CXXWRAPS := -Wl,--wrap,__cxa_pure_virtual -Wl,--wrap,__cxa_throw \
            -Wl,--wrap,__cxa_rethrow -Wl,--wrap,__cxa_allocate_exception \
            -Wl,--wrap,__cxa_free_exception -Wl,--wrap,__cxa_begin_catch \
            -Wl,--wrap,__cxa_end_catch -Wl,--wrap,__cxa_call_unexpected \
            -Wl,--wrap,__cxa_call_terminate -Wl,--wrap,__gxx_personality_v0 \
            -Wl,--wrap,_Unwind_Resume -Wl,--wrap,_ZSt19__throw_logic_errorPKc \
            -Wl,--wrap,_ZSt20__throw_length_errorPKc \
            -Wl,--wrap,_ZNSt11logic_errorC2EPKc -Wl,--wrap,exit

CXXREQUIRED := -Wl,--require-defined,__libnx_initheap \
               -Wl,--require-defined,__libnx_exception_handler \
               -Wl,--require-defined,__libnx_alloc \
               -Wl,--require-defined,__libnx_aligned_alloc \
               -Wl,--require-defined,__libnx_free \
               -Wl,--require-defined,__appInit \
               -Wl,--require-defined,__appExit \
               -Wl,--require-defined,argvSetup

LDFLAGS := -specs=$(STRAT_SPECS) -specs=$(DEVKITPRO)/libnx/switch.specs \
           -g $(ARCH) $(CXXWRAPS) $(CXXREQUIRED) \
           -Wl,-Map,$(TARGET).map -Wl,-z,relro,-z,now
LIBS := -lstratosphere -lnx
LIBDIRS := $(PORTLIBS) $(LIBNX) $(DEVKITPRO)/AtmosphereLibs

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export OUTDIR := $(CURDIR)/../../SdOut/atmosphere/contents/0100000000001000/exefs
export SYSTEM_JSON := $(CURDIR)/DeskNXSystem.json
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
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
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export APP_TITLE APP_AUTHOR APP_VERSION
.PHONY: all clean
all: $(OUTDIR)/main $(OUTDIR)/main.npdm

$(OUTDIR)/main: $(OUTPUT).nso | $(OUTDIR)
	@cp -f $< $@
	@echo "DeskNX qlaunch NSO staged: $@"

$(OUTDIR)/main.npdm: $(SYSTEM_JSON) | $(OUTDIR)
	@npdmtool $(SYSTEM_JSON) $@
	@echo "DeskNX qlaunch NPDM staged: $@"

$(OUTDIR):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nso $(TARGET).npdm $(TARGET).nsp $(OUTDIR)/main $(OUTDIR)/main.npdm
else
DEPENDS := $(OFILES:.o=.d)
.PHONY: all
all: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif
