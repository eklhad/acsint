#  Simple makefile to move to the subdirectories.
#  This only works if you are making the default target.

all :
	cd drivers ; make
	cd bridge ; make
	cd jupiter ; make

clean :
	cd drivers ; make clean
	cd bridge ; make clean
	cd jupiter ; make clean

