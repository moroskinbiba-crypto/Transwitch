# Standalone Tesla overlay. This target is isolated from DeskNX's launcher build.
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment")
endif

TOPDIR ?= $(CURDIR)
PORTLIBS := $(DEVKITPRO)/portlibs/switch
export PATH := $(PORTLIBS)/bin:$(PATH)
export PKG_CONFIG_PATH := $(PORTLIBS)/lib/pkgconfig:$(PKG_CONFIG_PATH)
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := nextendo-online
BUILD       := build
SOURCES     := source
INCLUDES    := include libs/libtesla/include
APP_TITLE   := Nextendo Online
APP_AUTHOR  := Transwitch
APP_VERSION := 0.1.0
NO_ICON     := 1

ARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS      := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections $(ARCH) -D__SWITCH__
CXXFLAGS    := $(CFLAGS) -std=gnu++20 -fno-exceptions
ASFLAGS     := -g $(ARCH)
LDFLAGS     = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,--gc-sections
CURL_LIBS   := $(shell pkg-config --static --libs libcurl 2>/dev/null)
ifeq ($(strip $(CURL_LIBS)),)
CURL_LIBS   := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz
endif
LIBS        := $(CURL_LIBS) -lz -lnx
LIBDIRS     := $(PORTLIBS) $(DEVKITPRO)/libnx

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export APP_TITLE APP_AUTHOR APP_VERSION
export NROFLAGS += --nacp=$(OUTPUT).nacp
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_SRC)
export HFILES     := $(foreach dir,$(INCLUDES),$(wildcard $(dir)/*.h)) \
                     $(foreach dir,$(INCLUDES),$(wildcard $(dir)/*.hpp))
export INCLUDE    := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                     $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                     -I$(CURDIR)/$(BUILD)
CFLAGS   += $(INCLUDE)
CXXFLAGS += $(INCLUDE)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export LIBS CFLAGS CXXFLAGS ASFLAGS LDFLAGS

.PHONY: all clean
all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).nacp

else
# The recursive make re-evaluates CFLAGS/CXXFLAGS. Re-add the absolute include
# paths exported by the outer invocation so source headers remain visible.
CFLAGS   += $(INCLUDE)
CXXFLAGS += $(INCLUDE)
DEPENDS := $(OFILES:.o=.d)

.PHONY: all
all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)
endif
