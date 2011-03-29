KERNELDIR = /lib/modules/$(shell uname -r)/build

obj-m += ttyclicks.o
obj-m += acsint.o

all : acstest

acstest : acstest.o acsbridge.o
	cc -o acstest acstest.o acsbridge.o

acstest.o acsbridge.o : acsint.h acsbridge.h

modules:
	make -C $(KERNELDIR) M=$(shell pwd)

modules_install:
	make -C $(KERNELDIR) M=$(shell pwd) INSTALL_MOD_DIR=acsint modules_install

clean:
	make -C $(KERNELDIR) M=$(shell pwd) clean

