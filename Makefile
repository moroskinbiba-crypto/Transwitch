# DeskNX native Nintendo Switch build entry point.
.PHONY: all native clean

all: native

native:
	@$(MAKE) --no-print-directory -f Makefile.switch

clean:
	@$(MAKE) --no-print-directory -f Makefile.switch clean
