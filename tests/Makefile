BRIDGEPATH = ../bridge
DRIVERPATH = ../driver

ifneq "$(BRIDGEPATH)" ""
CFLAGS += -I$(BRIDGEPATH)
LDFLAGS += -L$(BRIDGEPATH)
endif

ifneq "$(DRIVERPATH)" ""
	CFLAGS += -I$(DRIVERPATH)
	endif

LDLIBS = -lacs

all : acstest pipetest

acstest : acstest.o

pipetest : pipetest.o
