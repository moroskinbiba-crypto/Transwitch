#---------------------------------------------------------------------------------
# DeskNX - Top Level Makefile
# Directs build to Makefile.switch for devkitA64 / libnx NRO packaging
#---------------------------------------------------------------------------------
.PHONY: all clean nro

all:
	@$(MAKE) --no-print-directory -f Makefile.switch

nro:
	@$(MAKE) --no-print-directory -f Makefile.switch

clean:
	@$(MAKE) --no-print-directory -f Makefile.switch clean
