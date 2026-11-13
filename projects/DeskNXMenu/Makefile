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
# TOPDIR is exported by the outer invocation; fall back to CURDIR when this
# Makefile is invoked directly (e.g. `make -C projects/DeskNXMenu` in CI).
APP_JSON    = $(if $(TOPDIR),$(TOPDIR),$(CURDIR))/DeskNXMenu.json

ARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS      := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections $(ARCH) -D__SWITCH__ -DDESKNX_SYSTEM_APPLET
CFLAGS      += $(INCLUDE)
CXXFLAGS    := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions
ASFLAGS     := -g $(ARCH)
LDFLAGS     = -specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,--gc-sections
LIBS        := $(SDL2_IMAGE_LIBS) $(SDL2_TTF_LIBS) $(SDL2_LIBS) -lnx
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

{"content":"# DeskNX library applet (served by the optional HOME backend).\r\nifeq ($(strip $(DEVKITPRO)),)\r\n$(error \"Set DEVKITPRO to your devkitPro installation\")\r\nendif\r\n\r\nLIBNX    := $(DEVKITPRO)/libnx\r\nPORTLIBS := $(DEVKITPRO)/portlibs/switch\r\n# Cross-compiled SDL ports install their .pc files under the Switch portlibs prefix.\r\nexport PKG_CONFIG_PATH := $(PORTLIBS)/lib/pkgconfig:$(PKG_CONFIG_PATH)\r\nexport PATH := $(PORTLIBS)/bin:$(PATH)\r\n# Query each SDL component separately: a missing optional .pc file must not\r\n# discard linker flags for the components that are installed.\r\nSDL2_LIBS       := $(shell pkg-config --static --libs SDL2 2>/dev/null)\r\nSDL2_IMAGE_LIBS := $(shell pkg-config --static --libs SDL2_image 2>/dev/null)\r\nSDL2_TTF_LIBS   := $(shell pkg-config --static --libs SDL2_ttf 2>/dev/null)\r\nifeq ($(strip $(SDL2_LIBS)),)\r\nSDL2_LIBS := -lSDL2\r\nendif\r\nifeq ($(strip $(SDL2_IMAGE_LIBS)),)\r\nSDL2_IMAGE_LIBS := -lSDL2_image\r\nendif\r\nifeq ($(strip $(SDL2_TTF_LIBS)),)\r\nSDL2_TTF_LIBS := -lSDL2_ttf\r\nendif\r\n\r\ninclude $(LIBNX)/switch_rules\r\n\r\nTARGET      := DeskNXMenu\r\nBUILD       := build\r\nSOURCES     := ../../source ./source\r\nINCLUDES    := ../../include ../../projects/common/include\r\nAPP_TITLE   := DeskNX\r\nAPP_AUTHOR  := DeskNX contributors\r\nAPP_VERSION := 0.2.0\r\n# TOPDIR is exported by the outer invocation; fall back to CURDIR when this\r\n# Makefile is invoked directly (e.g. `make -C projects/DeskNXMenu` in CI).\r\nAPP_JSON    = $(if $(TOPDIR),$(TOPDIR),$(CURDIR))/DeskNXMenu.json\r\n\r\nARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE\r\nCFLAGS      := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections $(ARCH) -D__SWITCH__ -DDESKNX_SYSTEM_APPLET\r\nCFLAGS      += $(INCLUDE)\r\nCXXFLAGS    := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions\r\nASFLAGS     := -g $(ARCH)\r\nLDFLAGS     = -specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,--gc-sections\r\nLIBS        := $(SDL2_IMAGE_LIBS) $(SDL2_TTF_LIBS) $(SDL2_LIBS) -lnx\r\nLIBDIRS     := $(PORTLIBS) $(LIBNX)\r\n\r\nifneq ($(BUILD),$(notdir $(CURDIR)))\r\nexport OUTPUT := $(CURDIR)/$(TARGET)\r\nexport TOPDIR := $(CURDIR)\r\nexport APP_JSON := $(CURDIR)/DeskNXMenu.json\r\nexport OUTDIR := $(CURDIR)/../../SdOut/switch/DeskNX/bin/Menu\r\nexport VPATH := $(foreach dir,$(SOURCES),$(abspath $(CURDIR)/$(dir)))\r\nexport DEPSDIR := $(CURDIR)/$(BUILD)\r\nCPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))\r\nCFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))\r\nSFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))\r\nifeq ($(strip $(CPPFILES)),)\r\nexport LD := $(CC)\r\nelse\r\nexport LD := $(CXX)\r\nendif\r\nexport OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)\r\nexport OFILES := $(OFILES_SRC)\r\nexport HFILES := $(foreach dir,$(INCLUDES),$(wildcard $(dir)/*.h))\r\nexport INCLUDE := $(foreach dir,$(INCLUDES),-I$(abspath $(CURDIR)/$(dir))) \\\r\n                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)\r\nexport LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)\r\nexport APP_TITLE APP_AUTHOR APP_VERSION APP_JSON\r\n.PHONY: all clean\r\nall: $(BUILD)\r\n$(BUILD):\r\n\t@mkdir -p $@\r\n\t@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile\r\nclean:\r\n\t@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nso $(TARGET).npdm $(TARGET).nsp\r\n\t@rm -rf $(CURDIR)/../../SdOut/switch/DeskNX/bin/Menu\r\nelse\r\nDEPENDS := $(OFILES:.o=.d)\r\n.PHONY: all\r\nall: $(OUTDIR)/main $(OUTDIR)/main.npdm\r\n\r\n$(OUTPUT).elf: $(OFILES)\r\n\r\n$(OUTDIR)/main: $(OUTPUT).nso | $(OUTDIR)\r\n\t@cp -f $< $@\r\n\t@echo \"DeskNX applet NSO: $@\"\r\n\r\n$(OUTDIR)/main.npdm: $(APP_JSON) | $(OUTDIR)\r\n\t@npdmtool $(APP_JSON) $@\r\n\t@echo \"DeskNX applet NPDM: $@\"\r\n\r\n$(OUTDIR):\r\n\t@mkdir -p $@\r\n\r\n-include $(DEPENDS)\r\nendif\r\n"}