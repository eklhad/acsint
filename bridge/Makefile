#  acsint bridge library, between the device driver and the adapter.

#  When this was a shared library we needed fPIC
CFLAGS += -MMD

SRCS = acsbridge.c acsbind.c acstalk.c
OBJS = ${SRCS:.c=.o}

LIBNAME = libacs.a

#  This was the shared library; no longer implemented.
#${LIBTAG} : ${OBJS}
#	${CC} ${LDFLAGS} -shared -Wl,-soname,${LIBSONAME} -o ${LIBTAG} ${OBJS}

${LIBNAME} : ${OBJS}
	ar rs ${LIBNAME} $?

clean:
	rm -f $(OBJS) $(LIBNAME)

-include ${SRCS:.c=.d}
