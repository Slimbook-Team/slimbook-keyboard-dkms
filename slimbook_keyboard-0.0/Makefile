MODNAME = clevo_platform
MODVER = 0.0

# Kbuild part.
# This branch is used when the Makefile is parsed by the kernel build system,
# either for an in-place build or when building through DKMS.
ifneq ($(KERNELRELEASE),)

obj-m += $(MODNAME).o
#CFLAGS_clevo_acpi.o := -DDEBUG

else

# Standalone part, used when running make directly from this directory.
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(CURDIR)

MDIR = /usr/src/$(MODNAME)-$(MODVER)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

dkmsinstall:
	mkdir -p $(MDIR)
	cp Makefile dkms.conf $(wildcard *.c) $(wildcard *.h) $(MDIR)/.
	dkms add $(MODNAME)/$(MODVER)
	dkms build $(MODNAME)/$(MODVER)
	dkms install $(MODNAME)/$(MODVER)

dkmsuninstall:
	-rmmod $(MODNAME)
	-dkms uninstall $(MODNAME)/$(MODVER)
	-dkms remove $(MODNAME)/$(MODVER) --all
	rm -rf $(MDIR)

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all install dkmsinstall dkmsuninstall clean

endif
