KERNELDIR = /lib/modules/$(shell uname -r)/build

obj-m += ttyclicks.o
obj-m += acsint.o

#  You need to define NO_KDS if your kernel has not been patched
#  to support the new functions kd_mkpulse, kd_mkswoop, and kd_mknotes.

modules:
	make -C $(KERNELDIR) M=$(shell pwd) \
		CFLAGS_MODULE="-DCONFIG_TTYCLICKS -DNO_KDS"

modules_install:
	make -C $(KERNELDIR) M=$(shell pwd) INSTALL_MOD_DIR=acsint modules_install

clean:
	make -C $(KERNELDIR) M=$(shell pwd) clean
