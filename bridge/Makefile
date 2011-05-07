DRIVERINCLUDE = ../driver

#  bridge objects
BOBJS = acsbridge.o acsbind.o acstalk.o

CFLAGS += -I$(DRIVERINCLUDE)

libacs.a : $(BOBJS)
	ar rs libacs.a $?

$(BOBJS) : $(DRIVERINCLUDE)/acsint.h acsbridge.h
