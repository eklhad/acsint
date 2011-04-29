KERNELDIR = /lib/modules/$(shell uname -r)/build

obj-m += ttyclicks.o
obj-m += acsint.o

all : acstest pipetest

acstest : acstest.o libacs.a
	cc -o acstest acstest.o libacs.a

pipetest : pipetest.o libacs.a
	cc -o pipetest pipetest.o libacs.a

#  bridge objects
BOBJS = acsbridge.o acsbind.o acstalk.o
$(BOBJS) : acsint.h acsbridge.h

libacs.a : $(BOBJS)
	ar rs libacs.a $?

modules:
	make -C $(KERNELDIR) M=$(shell pwd)

modules_install:
	make -C $(KERNELDIR) M=$(shell pwd) INSTALL_MOD_DIR=acsint modules_install

clean:
	make -C $(KERNELDIR) M=$(shell pwd) clean

