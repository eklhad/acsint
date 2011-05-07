DRIVERPATH = ../driver

DEPFLAG = -MMD
CFLAGS += $(DEPFLAG)

ifneq "$(DRIVERPATH)" ""
CFLAGS += -I$(DRIVERPATH)
endif

#  bridge objects
BOBJS = acsbridge.o acsbind.o acstalk.o

libacs.a : $(BOBJS)
	ar rs libacs.a $?

-include $(BOBJS:.o=.d)
