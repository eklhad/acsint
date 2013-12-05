KERNELDIR = /lib/modules/$(shell uname -r)/build

obj-m += ttyclicks.o
obj-m += acsint.o
obj-m += hq_kern.o

#  You need to define NO_KDS if your kernel has not been patched
#  to support the new functions kd_mkpulse, kd_mkswoop, and kd_mknotes.
#  See the "patch" directory of this project.
#  Sadly, my sound patch has not been accepted into any kernel yet.
#  So I have defined the symbol in the source itself.
#  Makes it easier to move ttyclicks.c in and out of various source trees.
#  If I had not done that, you would need this line.
#		CFLAGS_MODULE="-DCONFIG_TTYCLICKS -DNO_KDS"

modules:
	make -C $(KERNELDIR) M=$(shell pwd)

modules_install:
	make -C $(KERNELDIR) M=$(shell pwd) INSTALL_MOD_DIR=acsint modules_install

clean:
	make -C $(KERNELDIR) M=$(shell pwd) clean
