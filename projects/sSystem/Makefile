# DeskNX qlaunch replacement sysmodule.
# This target requires libstratosphere/AtmosphereLibs built for the same
# Atmosphere revision as the console. It is intentionally separate from NRO CI.
ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO to your devkitPro installation")
endif

LIBNX ?= $(DEVKITPRO)/libnx
ATMOSPHERE_DIR ?= /opt/atmosphere
export ATMOSPHERE_LIBRARIES_DIR := $(ATMOSPHERE_DIR)/libraries
STRAT_SPECS := $(ATMOSPHERE_LIBRARIES_DIR)/libstratosphere/stratosphere.specs

include $(LIBNX)/switch_rules

TARGET       := DeskNXSystem
BUILD        := build
SOURCE_DIR   := source
SYSTEM_JSON  := $(CURDIR)/DeskNXSystem.json
OUTDIR       := $(abspath ../../SdOut/atmosphere/contents/0100000000001000/exefs)

APP_TITLE    := DeskNXSystem
APP_AUTHOR   := DeskNX contributors
APP_VERSION  := 0.2.0

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
ATMOSPHERE_DEFINES := -DATMOSPHERE -DATMOSPHERE_ARCH_ARM64 -DATMOSPHERE_ARCH_ARM_V8A \
                      -DATMOSPHERE_BOARD_NINTENDO_NX -DATMOSPHERE_OS_HORIZON -D__SWITCH__ \
                      -DATMOSPHERE_IS_STRATOSPHERE -D_GNU_SOURCE -DSL_LIBNX_MASTER
INCLUDE := -I$(CURDIR)/include -I$(CURDIR)/../../projects/common/include \
           -I$(DEVKITPRO)/AtmosphereLibs/include -I$(LIBNX)/include -I$(PORTLIBS)/include
CFLAGS := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections -fno-strict-aliasing \
          -fwrapv -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector \
          -Wno-format-zero-length -Wno-stringop-truncation -Wno-format-truncation \
          $(ARCH) $(INCLUDE) $(ATMOSPHERE_DEFINES)
CXXFLAGS := $(CFLAGS) -std=gnu++23 -fno-rtti -fno-exceptions \
            -DSL_VERSION=\"$(APP_VERSION)\"
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

LDFLAGS := -specs=$(STRAT_SPECS) -specs=$(LIBNX)/switch.specs \
           -g $(ARCH) $(CXXWRAPS) $(CXXREQUIRED) \
           -Wl,-Map,$(BUILD)/$(TARGET).map -Wl,-z,relro,-z,now
LIBDIRS := $(PORTLIBS) $(LIBNX) $(DEVKITPRO)/AtmosphereLibs
LIBS := -lstratosphere -lnx

CPPFILES := $(wildcard $(SOURCE_DIR)/*.cpp)
OFILES := $(patsubst $(SOURCE_DIR)/%.cpp,$(BUILD)/source/%.o,$(CPPFILES))
DEPENDS := $(OFILES:.o=.d)

.PHONY: all clean
all: $(OUTDIR)/main $(OUTDIR)/main.npdm

$(BUILD)/source/%.o: $(SOURCE_DIR)/%.cpp | $(BUILD)/source
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/source:
	@mkdir -p $@

$(BUILD)/$(TARGET).elf: $(OFILES) | $(BUILD)
	@$(CXX) $(LDFLAGS) -o $@ $^ $(foreach lib,$(LIBDIRS),-L$(lib)/lib) $(LIBS)
	@$(NM) -CSn $@ > $(BUILD)/$(TARGET).lst

$(BUILD)/$(TARGET).nso: $(BUILD)/$(TARGET).elf
	@elf2nso $< $@
	@echo "Built sysmodule NSO: $@"

$(OUTDIR)/main: $(BUILD)/$(TARGET).nso | $(OUTDIR)
	@cp -f $< $@
	@echo "Staged qlaunch override NSO: $@"

$(OUTDIR)/main.npdm: $(SYSTEM_JSON) | $(OUTDIR)
	@npdmtool $(SYSTEM_JSON) $@
	@echo "Staged qlaunch override NPDM: $@"

$(BUILD):
	@mkdir -p $@

$(OUTDIR):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD) $(OUTDIR)/main $(OUTDIR)/main.npdm

-include $(DEPENDS)
