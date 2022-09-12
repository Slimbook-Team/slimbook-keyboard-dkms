MODNAME = clevo_platform
MODVER = 0.0
obj-m += clevo_platform.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
#CFLAGS_clevo_acpi.o := -DDEBUG
                                                          
MDIR = /usr/src/$(MODNAME)-$(MODVER)

all:
	make -C $(KDIR) M=$(PWD) modules

install:
	make -C $(KDIR) M=$(PWD) modules_install

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
	make -C $(KDIR) M=$(PWD) clean