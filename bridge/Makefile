package = acsbridge
prefix = /usr/local
exec_prefix = ${prefix}
datarootdir = ${prefix}/share
datadir = ${datarootdir}
docdir = ${datarootdir}/doc/${package}
includedir = ${prefix}/include
libdir = ${exec_prefix}/lib
 
DRIVERPATH = ../drivers

PICFLAG = -fPIC
DEPFLAG = -MMD
CFLAGS += ${PICFLAG} ${DEPFLAG}

ifneq "${DRIVERPATH}" ""
CFLAGS += -I${DRIVERPATH}
endif

INCLUDES = acsbridge.h
SRCS = acsbridge.c acsbind.c acstalk.c
OBJS = ${SRCS:.c=.o}

# These are the shared library version numbers for libacs.
# They should be left at 0 until we do a public release. Once that is
# done, they should be the very last things to be updated before each
# release, and they should be updated as follows:
#
# If programs using the previous version may use the
# new version as a drop-in replacement, and programs using the
# new version can also work with the previous one, only increment
# revision.
#
#If programs using the previous version can use the new version as a
# drop-in replacement but programs using the new version can use APIs
# that are not present in the previous version, increment CURRENT and
# AGE and set revision to 0.
#
# If programs need to be changed, recompiled and relinked to use the new
# version, increment current and set revision and age to 0.
CURRENT = 0
REVISION = 0
AGE = 0

LIBNAME = libacs.so
LIBSONAME = ${LIBNAME}.${CURRENT}
LIBTAG = ${LIBNAME}.${CURRENT}.${REVISION}.${AGE}

INSTALL = install
INSTALL_PROGRAM = ${INSTALL}
INSTALL_DATA = ${INSTALL} -m 644

${LIBTAG} : ${OBJS}
	${CC} ${LDFLAGS} -shared -Wl,-soname,${LIBSONAME} -o ${LIBTAG} ${OBJS}

install: ${LIBTAG}
	${INSTALL} -d ${DESTDIR}${includedir}/acsbridge
	${INSTALL_DATA} ${INCLUDES}  ${DESTDIR}${includedir}/acsbridge
	${INSTALL} -d ${DESTDIR}${libdir}
	${INSTALL_PROGRAM} ${LIBTAG}  ${DESTDIR}${libdir}
	ln -sf ${LIBTAG} ${DESTDIR}${libdir}/${LIBSONAME}
	ln -sf ${LIBSONAME} ${DESTDIR}${libdir}/${LIBNAME}

uninstall:
	rm -fr ${DESTDIR}${includedir}/acsbridge
	rm ${DESTDIR}${libdir}/${LIBTAG}
	rm ${DESTDIR}${libdir}/${LIBSONAME}
	rm ${DESTDIR}${libdir}/${LIBNAME}

-include ${SRCS:.c=.d}
