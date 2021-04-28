#
# Makefile for Adlink dio misc driver
#

PWD := $(shell pwd)

obj-m := adlink-dio.o

ifeq ($(KERNELRELEASE),)
all:
	@echo "Compiling out-of-tree kernel module for adlink-dio"
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD)

modules_install:
	@echo "Install out-of-tree adlink-dio kernel module"
	$(MAKE) INSTALL_MOD_STRIP=1 INSTALL_MOD_PATH=$(MODLIB) INSTALL_MOD_DIR=extra -C $(KERNEL_SRC) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean

endif
