KERNELDIR = /lib/modules/$(shell uname -r)/build

obj-m += ttyclicks.o
obj-m += acsint.o
obj-m += hq_kern.o

modules:
	make -C $(KERNELDIR) M=$(shell pwd)

modules_install:
	make -C $(KERNELDIR) M=$(shell pwd) INSTALL_MOD_DIR=acsint modules_install

clean:
	make -C $(KERNELDIR) M=$(shell pwd) clean
