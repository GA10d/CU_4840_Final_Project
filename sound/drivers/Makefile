ifneq ($(KERNELRELEASE),)
include Kbuild
else
KDIR ?= /src/Altera/linux-socfpga
ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-

.PHONY: all modules clean

all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
endif
