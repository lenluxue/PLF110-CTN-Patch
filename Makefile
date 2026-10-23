KDIR ?=
PWD := $(shell pwd)

.PHONY: all clean

all:
	@test -n "$(KDIR)" || (echo "KDIR must point to a prepared kernel build tree" >&2; exit 1)
	$(MAKE) -C "$(KDIR)" M="$(PWD)" ARCH=arm64 LLVM=1 modules

clean:
	@test -n "$(KDIR)" || (echo "KDIR must point to a prepared kernel build tree" >&2; exit 1)
	$(MAKE) -C "$(KDIR)" M="$(PWD)" ARCH=arm64 LLVM=1 clean
