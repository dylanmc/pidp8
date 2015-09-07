INCLUDEFLAGS=-I/opt/vc/include -I/opt/vc/include/interface/vmcs_host/linux -I/opt/vc/include/interface/vcos/pthreads -I..
LIBFLAGS=-L/opt/vc/lib -lGLESv2 -lEGL -lbcm_host -lpthread  -ljpeg

all: vc8e

# TEMPORARY
# assumes compilation in openvg/client directory after openvg has been installed
# see github: ajstarks/openvg

vc8e:	vc8e.c ../libshapes.o ../oglinit.o
	gcc -Wall $(INCLUDEFLAGS) $(LIBFLAGS) -o  sw sw.c ../libshapes.o ../oglinit.o

