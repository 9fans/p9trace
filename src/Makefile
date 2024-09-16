CC=gcc
CFLAGS= -g -Wall

OFILES=\
	super.o\
	trace.o\
	inflate.o\


super: $(OFILES)
	$(CC) $(CFLAGS) -o super $(OFILES)

$(OFILES): trace.h

clean:
	rm -f *.o super

